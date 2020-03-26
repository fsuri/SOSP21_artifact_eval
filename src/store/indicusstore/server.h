// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/indicusstore/server.h:
 *   A single transactional server replica.
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

#ifndef _INDICUS_SERVER_H_
#define _INDICUS_SERVER_H_

#include "bft_tapir/config.h"
#include "replication/ir/replica.h"
#include "store/server.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/indicusstore/common.h"
#include "store/indicusstore/store.h"
#include "store/indicusstore/indicus-proto.pb.h"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace indicusstore {

class ServerTest;

enum OCCType {
  TAPIR = 0,
  MVTSO = 1
};

class Server : public TransportReceiver, public ::Server {
 public:
  Server(const transport::Configuration &config, int groupIdx, int idx,
      Transport *transport, KeyManager *keyManager, bool signedMessages,
      bool validateProofs, uint64_t timeDelta, OCCType occType,
      TrueTime timeServer = TrueTime(0, 0));
  virtual ~Server();

  virtual void ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data,
      void *meta_data) override;

  virtual void Load(const string &key, const string &value,
      const Timestamp timestamp) override;

  virtual inline Stats &GetStats() override { return stats; }

 private:
  friend class ServerTest;
  struct Value {
    std::string val;
    proto::CommittedProof proof;
  };

  void HandleRead(const TransportAddress &remote, const proto::Read &msg);
  void HandlePhase1(const TransportAddress &remote,
      const proto::Phase1 &msg);
  void HandlePhase2(const TransportAddress &remote,
      const proto::Phase2 &msg);
  void HandleWriteback(const TransportAddress &remote,
      const proto::Writeback &msg);
  void HandleAbort(const TransportAddress &remote, const proto::Abort &msg);

  proto::Phase1Reply::ConcurrencyControlResult DoOCCCheck(
      const std::string &txnDigest, const proto::Transaction &txn,
      Timestamp &retryTs, proto::CommittedProof &conflict);
  proto::Phase1Reply::ConcurrencyControlResult DoTAPIROCCCheck(
      const std::string &txnDigest, const proto::Transaction &txn,
      Timestamp &retryTs);
  proto::Phase1Reply::ConcurrencyControlResult DoMVTSOOCCCheck(
      const std::string &txnDigest, const proto::Transaction &txn,
      proto::CommittedProof &conflict);

  void GetPreparedWriteTimestamps(
      std::unordered_map<std::string, std::set<Timestamp>> &writes);
  void GetPreparedWrites(
      std::unordered_map<std::string, std::vector<proto::Transaction>> &writes);
  void GetPreparedReadTimestamps(
      std::unordered_map<std::string, std::set<Timestamp>> &reads);
  void GetPreparedReads(
      std::unordered_map<std::string, std::vector<proto::Transaction>> &reads);
  void Prepare(const std::string &txnDigest, const proto::Transaction &txn);
  void GetCommittedWrites(const std::string &key, const Timestamp &ts,
      std::vector<std::pair<Timestamp, Value>> &writes);
  void GetCommittedReads(const std::string &key,
      std::set<std::pair<Timestamp, Timestamp>> &reads);
  void Commit(const std::string &txnDigest, const proto::Transaction &txn);
  void Abort(const std::string &txnDigest);
  void CheckDependents(const std::string &txnDigest);
  bool CheckHighWatermark(const Timestamp &ts);

  const transport::Configuration &config;
  const int groupIdx;
  const int idx;
  const int id;
  Transport *transport;
  const OCCType occType;
  const bool signedMessages;
  const bool validateProofs;
  KeyManager *keyManager;
  const uint64_t timeDelta;
  TrueTime timeServer;

  VersionedKVStore<Timestamp, Value> store;
  // Key -> V
  std::unordered_map<std::string, std::set<std::pair<Timestamp, Timestamp>>> committedReads;
  std::unordered_map<std::string, std::map<std::string, std::set<Timestamp>>> rts;
  // Digest -> V
  std::unordered_map<std::string, std::pair<Timestamp, proto::Transaction>> prepared;
  std::unordered_map<std::string, proto::Phase1Reply::ConcurrencyControlResult> p1Decisions;
  std::unordered_map<std::string, proto::CommitDecision> p2Decisions;
  std::unordered_set<std::string> committed;
  std::unordered_set<std::string> aborted;
  std::unordered_map<std::string, std::unordered_set<std::string>> depends; // V depends on K

  Stats stats;
  std::unordered_set<std::string> active;
};

} // namespace indicusstore

#endif /* _INDICUS_SERVER_H_ */
