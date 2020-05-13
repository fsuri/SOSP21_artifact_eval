// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/indicus/shardclient.h:
 *   Single shard indicus transactional client interface.
 *
 * Copyright 2015 Irene Zhang <iyzhang@cs.washington.edu>
 *                Naveen Kr. Sharma <naveenks@cs.washington.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#ifndef _INDICUS_SHARDCLIENT_H_
#define _INDICUS_SHARDCLIENT_H_

#include "lib/keymanager.h"
#include "lib/assert.h"
#include "lib/configuration.h"
#include "lib/crypto.h"
#include "lib/message.h"
#include "lib/transport.h"
#include "replication/ir/client.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/common/transaction.h"
#include "store/common/frontend/txnclient.h"
#include "store/common/common-proto.pb.h"
#include "store/indicusstore/indicus-proto.pb.h"
#include "store/indicusstore/phase1validator.h"
#include "store/common/pinginitiator.h"

#include <map>
#include <string>
#include <vector>

namespace indicusstore {

typedef std::function<void(int, const std::string &,
    const std::string &, const Timestamp &, const proto::Dependency &,
    bool, bool)> read_callback;
typedef std::function<void(int, const std::string &)> read_timeout_callback;

typedef std::function<void(proto::CommitDecision, bool,
    const proto::CommittedProof &,
    const std::map<proto::ConcurrencyControl::Result, proto::Signatures> &)> phase1_callback;
typedef std::function<void(int)> phase1_timeout_callback;

typedef std::function<void(const proto::Signatures &)> phase2_callback;
typedef std::function<void(int)> phase2_timeout_callback;

typedef std::function<void()> writeback_callback;
typedef std::function<void(int)> writeback_timeout_callback;

class ShardClient : public TransportReceiver, public PingInitiator, public PingTransport {
 public:
  ShardClient(transport::Configuration *config, Transport *transport,
      uint64_t client_id, int group, const std::vector<int> &closestReplicas,
      bool pingReplicas,
      Parameters params, KeyManager *keyManager, TrueTime &timeServer);
  virtual ~ShardClient();

  virtual void ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data,
      void *meta_data) override;

  // Begin a transaction.
  virtual void Begin(uint64_t id);

  // Get the value corresponding to key.
  virtual void Get(uint64_t id, const std::string &key, const TimestampMessage &ts,
      uint64_t readMessages, uint64_t rqs, uint64_t rds, read_callback gcb,
      read_timeout_callback gtcb, uint32_t timeout);

  // Set the value for the given key.
  virtual void Put(uint64_t id, const std::string &key,
      const std::string &value, put_callback pcb, put_timeout_callback ptcb,
      uint32_t timeout);

  virtual void Phase1(uint64_t id, const proto::Transaction &transaction,
      const std::string &txnDigest,
      phase1_callback pcb, phase1_timeout_callback ptcb, uint32_t timeout);
  virtual void Phase2(uint64_t id, const proto::Transaction &transaction,
      const std::string &txnDigest, proto::CommitDecision decision,
      const proto::GroupedSignatures &groupedSigs, phase2_callback pcb,
      phase2_timeout_callback ptcb, uint32_t timeout);
  virtual void Writeback(uint64_t id, const proto::Transaction &transaction,
      const std::string &txnDigest,
      proto::CommitDecision decision, bool fast,
      const proto::CommittedProof &conflict,
      const proto::GroupedSignatures &p1Sigs,
      const proto::GroupedSignatures &p2Sigs);

  virtual void Abort(uint64_t id, const TimestampMessage &ts);
  virtual bool SendPing(size_t replica, const PingMessage &ping); 
 private:
  struct PendingQuorumGet {
    PendingQuorumGet(uint64_t reqId) : reqId(reqId),
        numReplies(0UL), numOKReplies(0UL), hasDep(false),
        firstCommittedReply(true) { }
    ~PendingQuorumGet() { }
    uint64_t reqId;
    std::string key;
    Timestamp rts;
    uint64_t rqs;
    uint64_t rds;
    Timestamp maxTs;
    std::string maxValue;
    uint64_t numReplies;
    uint64_t numOKReplies;
    std::map<Timestamp, std::pair<proto::Write, uint64_t>> prepared;
    std::map<Timestamp, proto::Signatures> preparedSigs;
    proto::Dependency dep;
    bool hasDep;
    read_callback gcb;
    read_timeout_callback gtcb;
    bool firstCommittedReply;
  };

  struct PendingPhase1 {
    PendingPhase1(uint64_t reqId, int group, const proto::Transaction &txn,
        const std::string &txnDigest, const transport::Configuration *config,
        KeyManager *keyManager, Parameters params) :
        reqId(reqId), requestTimeout(nullptr), decisionTimeout(nullptr),
        decisionTimeoutStarted(false), txn_(txn), txnDigest_(txnDigest),
        p1Validator(group, &txn_, &txnDigest_, config, keyManager, params),
        decision(proto::ABORT), fast(false) { }
    ~PendingPhase1() {
      if (requestTimeout != nullptr) {
        delete requestTimeout;
      }
      if (decisionTimeout != nullptr) {
        delete decisionTimeout;
      }
    }
    uint64_t reqId;
    Timeout *requestTimeout;
    Timeout *decisionTimeout;
    bool decisionTimeoutStarted;
    std::map<proto::ConcurrencyControl::Result, proto::Signatures> p1ReplySigs;
    phase1_callback pcb;
    phase1_timeout_callback ptcb;
    proto::Transaction txn_;
    std::string txnDigest_;
    Phase1Validator p1Validator;
    proto::CommitDecision decision;
    bool fast;
    proto::CommittedProof conflict;
  };

  struct PendingPhase2 {
    PendingPhase2(uint64_t reqId, proto::CommitDecision decision) : reqId(reqId),
        decision(decision), requestTimeout(nullptr), matchingReplies(0UL) { }
    ~PendingPhase2() {
      if (requestTimeout != nullptr) {
        delete requestTimeout;
      }
    }
    uint64_t reqId;
    proto::CommitDecision decision;
    Timeout *requestTimeout;

    proto::Signatures p2ReplySigs;
    uint64_t matchingReplies;
    phase2_callback pcb;
    phase2_timeout_callback ptcb;
  };

  struct PendingAbort {
    PendingAbort(uint64_t reqId) : reqId(reqId),
        requestTimeout(nullptr) { }
    ~PendingAbort() {
      if (requestTimeout != nullptr) {
        delete requestTimeout;
      }
    }
    uint64_t reqId;
    proto::Transaction txn;
    Timeout *requestTimeout;
    abort_callback acb;
    abort_timeout_callback atcb;
  };

  bool BufferGet(const std::string &key, read_callback rcb);

  /* Timeout for Get requests, which only go to one replica. */
  void GetTimeout(uint64_t reqId);

  /* Callbacks for hearing back from a shard for an operation. */
  void HandleReadReply(const proto::ReadReply &readReply);
  void HandlePhase1Reply(const proto::Phase1Reply &phase1Reply);
  void HandlePhase2Reply(const proto::Phase2Reply &phase2Reply);

  void Phase1Decision(uint64_t reqId);
  void Phase1Decision(
      std::unordered_map<uint64_t, PendingPhase1 *>::iterator itr);

  inline size_t GetNthClosestReplica(size_t idx) const {
    if (pingReplicas && GetOrderedReplicas().size() > 0) {
      return GetOrderedReplicas()[idx];
    } else {
      return closestReplicas[idx];
    }
  }

  const uint64_t client_id; // Unique ID for this client.
  Transport *transport; // Transport layer.
  transport::Configuration *config;
  const int group; // which shard this client accesses
  TrueTime &timeServer;
  const bool pingReplicas;
  const Parameters params;
  KeyManager *keyManager;
  const uint64_t phase1DecisionTimeout;
  std::vector<int> closestReplicas;

  uint64_t lastReqId;
  proto::Transaction txn;
  std::map<std::string, std::string> readValues;

  std::unordered_map<uint64_t, PendingQuorumGet *> pendingGets;
  std::unordered_map<uint64_t, PendingPhase1 *> pendingPhase1s;
  std::unordered_map<uint64_t, PendingPhase2 *> pendingPhase2s;
  std::unordered_map<uint64_t, PendingAbort *> pendingAborts;

  proto::Read read;
  proto::Phase1 phase1;
  proto::Phase2 phase2;
  proto::Writeback writeback;
  proto::Abort abort;
  proto::ReadReply readReply;
  proto::Phase1Reply phase1Reply;
  proto::Phase2Reply phase2Reply;
  PingMessage ping;
  
  proto::Write validatedPrepared;
  proto::ConcurrencyControl validatedCC;
  proto::Phase2Decision validatedP2Decision;
};

} // namespace indicusstore

#endif /* _INDICUS_SHARDCLIENT_H_ */
