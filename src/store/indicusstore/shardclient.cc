// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/indicusstore/groupclient.cc:
 *   Single group indicus transactional client.
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

#include "store/indicusstore/shardclient.h"

#include <google/protobuf/util/message_differencer.h>

#include "store/indicusstore/common.h"

namespace indicusstore {

ShardClient::ShardClient(transport::Configuration *config, Transport *transport,
    uint64_t client_id, int group, const std::vector<int> &closestReplicas_,
    bool signedMessages, bool validateProofs, bool hashDigest,
    KeyManager *keyManager, TrueTime &timeServer) :
    client_id(client_id), transport(transport), config(config), group(group),
    timeServer(timeServer),
    signedMessages(signedMessages), validateProofs(validateProofs),
    hashDigest(hashDigest),
    keyManager(keyManager), phase1DecisionTimeout(1000UL), lastReqId(0UL) {
  transport->Register(this, *config, -1, -1);

  if (closestReplicas_.size() == 0) {
    for  (int i = 0; i < config->n; ++i) {
      closestReplicas.push_back((i + client_id) % config->n);
    }
  } else {
    closestReplicas = closestReplicas;
  }
}

ShardClient::~ShardClient() {
}

void ShardClient::ReceiveMessage(const TransportAddress &remote,
      const std::string &t, const std::string &d, void *meta_data) {
  proto::SignedMessage signedMessage;
  proto::PackedMessage packedMessage;
  proto::ReadReply readReply;
  proto::Phase1Reply phase1Reply;
  proto::Phase2Reply phase2Reply;

  const std::string *type;
  const std::string *data;
  std::string signedType;
  std::string signedData;
  if (t == signedMessage.GetTypeName()) {
    if (!signedMessage.ParseFromString(d)) {
      return;
    }

    type = &signedType;
    data = &signedData;
    if (!ValidateSignedMessage(signedMessage, keyManager, signedData, signedType)) {
      return;
    }
  } else {
    type = &t;
    data = &d;
  }

  if (*type == readReply.GetTypeName()) {
    readReply.ParseFromString(*data);
    HandleReadReply(readReply);
  } else if (*type == phase1Reply.GetTypeName()) {
    phase1Reply.ParseFromString(*data);
    HandlePhase1Reply(phase1Reply, signedMessage);
  } else if (*type == phase2Reply.GetTypeName()) {
    phase2Reply.ParseFromString(*data);
    HandlePhase2Reply(phase2Reply, signedMessage);
  } else {
    Panic("Received unexpected message type: %s", type->c_str());
  }
}

void ShardClient::Begin(uint64_t id) {
  Debug("[group %i] BEGIN: %lu", group, id);

  txn = proto::Transaction();
  readValues.clear();
}

void ShardClient::Get(uint64_t id, const std::string &key,
    const TimestampMessage &ts, uint64_t readMessages, uint64_t rqs,
    uint64_t rds, read_callback gcb, read_timeout_callback gtcb,
    uint32_t timeout) {
  if (BufferGet(key, gcb)) {
    Debug("[group %i] read from buffer.", group);
    return;
  }

  uint64_t reqId = lastReqId++;
  PendingQuorumGet *pendingGet = new PendingQuorumGet(reqId);
  pendingGets[reqId] = pendingGet;
  pendingGet->key = key;
  pendingGet->rqs = rqs;
  pendingGet->rds = rds;
  pendingGet->gcb = gcb;
  pendingGet->gtcb = gtcb;

  proto::Read read;
  read.set_req_id(reqId);
  read.set_key(key);
  *read.mutable_timestamp() = ts;

  UW_ASSERT(rqs <= closestReplicas.size());
  for (size_t i = 0; i < readMessages; ++i) {
    Debug("[group %i] Sending GET to replica %d", group, closestReplicas[i]);
    transport->SendMessageToReplica(this, group, closestReplicas[i], read);
  }

  Debug("[group %i] Sent GET [%lu : %lu]", group, id, reqId);
}

void ShardClient::Put(uint64_t id, const std::string &key,
      const std::string &value, put_callback pcb, put_timeout_callback ptcb,
      uint32_t timeout) {
  WriteMessage *writeMsg = txn.add_write_set();
  writeMsg->set_key(key);
  writeMsg->set_value(value);
  pcb(REPLY_OK, key, value);
}

void ShardClient::Phase1(uint64_t id, const proto::Transaction &transaction,
    const std::string &txnDigest,
    phase1_callback pcb, phase1_timeout_callback ptcb, uint32_t timeout) {
  Debug("[group %i] Sending PHASE1 [%lu]", group, id);
  uint64_t reqId = lastReqId++;
  PendingPhase1 *pendingPhase1 = new PendingPhase1(reqId, &transaction,
      &txnDigest, config, keyManager, signedMessages, hashDigest);
  pendingPhase1s[reqId] = pendingPhase1;
  pendingPhase1->pcb = pcb;
  pendingPhase1->ptcb = ptcb;
  pendingPhase1->transaction = transaction;
  pendingPhase1->requestTimeout = new Timeout(transport, timeout, [this, pendingPhase1]() {
      phase1_timeout_callback ptcb = pendingPhase1->ptcb;
      auto itr = this->pendingPhase1s.find(pendingPhase1->reqId);
      if (itr != this->pendingPhase1s.end()) {
        PendingPhase1 *pendingPhase1 = itr->second;
        this->pendingPhase1s.erase(itr);
        delete pendingPhase1;
      }
      ptcb(REPLY_TIMEOUT);
  });

  // create prepare request
  proto::Phase1 phase1;
  phase1.set_req_id(reqId);
  *phase1.mutable_txn() = transaction;

  transport->SendMessageToGroup(this, group, phase1);

  pendingPhase1->requestTimeout->Reset();
}

void ShardClient::Phase2(uint64_t id,
    const proto::Transaction &txn,
    const std::map<int, std::vector<proto::Phase1Reply>> &groupedPhase1Replies,
    const std::map<int, std::vector<proto::SignedMessage>> &groupedSignedPhase1Replies,
    proto::CommitDecision decision, phase2_callback pcb,
    phase2_timeout_callback ptcb, uint32_t timeout) {
  Debug("[group %i] Sending PHASE2 [%lu]", group, id);
  uint64_t reqId = lastReqId++;
  PendingPhase2 *pendingPhase2 = new PendingPhase2(reqId, decision);
  pendingPhase2s[reqId] = pendingPhase2;
  pendingPhase2->pcb = pcb;
  pendingPhase2->ptcb = ptcb;
  pendingPhase2->requestTimeout = new Timeout(transport, timeout, [this, pendingPhase2]() {
      phase2_timeout_callback ptcb = pendingPhase2->ptcb;
      auto itr = this->pendingPhase2s.find(pendingPhase2->reqId);
      if (itr != this->pendingPhase2s.end()) {
        PendingPhase2 *pendingPhase2 = itr->second;
        this->pendingPhase2s.erase(itr);
        delete pendingPhase2;
      }

      ptcb(REPLY_TIMEOUT);
  });

  // create prepare request
  proto::Phase2 phase2;
  phase2.set_req_id(reqId);
  if (validateProofs) {
    *phase2.mutable_txn() = txn;
    if (signedMessages) {
      for (const auto &group : groupedSignedPhase1Replies) {
        for (const auto &reply : group.second) {
          *(*phase2.mutable_signed_p1_replies()->mutable_replies())[group.first].add_msgs() = reply;
        }

      }
    } else {
      for (const auto &group : groupedPhase1Replies) {
        for (const auto &reply : group.second) {
          *(*phase2.mutable_p1_replies()->mutable_replies())[group.first].add_replies() = reply;
        }
      }
    }
  } else {
    phase2.set_decision(decision);
  }

  transport->SendMessageToGroup(this, group, phase2);

  pendingPhase2->requestTimeout->Reset();
}

void ShardClient::Writeback(uint64_t id, const proto::Transaction &transaction,
    const std::string &txnDigest,
    proto::CommitDecision decision, const proto::CommittedProof &proof) {

  // create commit request
  proto::Writeback writeback;
  writeback.set_decision(decision);
  if (validateProofs) {
    *writeback.mutable_proof() = proof;
  }
  if (decision == proto::COMMIT) {
    if (!validateProofs) {
      *writeback.mutable_txn() = transaction;
    }
  } else {
    writeback.set_txn_digest(txnDigest);
  }
  
  transport->SendMessageToGroup(this, group, writeback);
  Debug("[group %i] Sent WRITEBACK[%lu]", group, id);
}

void ShardClient::Abort(uint64_t id, const TimestampMessage &ts) {
  proto::Abort abort;
  *abort.mutable_ts() = ts;
  for (const auto &read : txn.read_set()) {
    *abort.add_read_set() = read.key();
  }

  if (signedMessages) {
    proto::SignedMessage signedMessage;
    SignMessage(abort, keyManager->GetPrivateKey(client_id), client_id,
        signedMessage);
    transport->SendMessageToGroup(this, group, signedMessage);
  } else {
    transport->SendMessageToGroup(this, group, abort);
  }

  Debug("[group %i] Sent ABORT[%lu]", group, id);
}

bool ShardClient::BufferGet(const std::string &key, read_callback rcb) {
  for (const auto &write : txn.write_set()) {
    if (write.key() == key) {
      Debug("[group %i] Key %s was written with val %s.", group,
          BytesToHex(key, 16).c_str(), BytesToHex(write.value(), 16).c_str());
      rcb(REPLY_OK, key, write.value(), Timestamp(), proto::Dependency(),
          false, false);
      return true;
    }
  }

  for (const auto &read : txn.read_set()) {
    if (read.key() == key) {
      Debug("[group %i] Key %s was already read with ts %lu.%lu.", group,
          BytesToHex(key, 16).c_str(), read.readtime().timestamp(),
          read.readtime().id());
      rcb(REPLY_OK, key, readValues[key], read.readtime(), proto::Dependency(),
          false, false);
      return true;
    }
  }

  return false;
}

void ShardClient::GetTimeout(uint64_t reqId) {
  auto itr = this->pendingGets.find(reqId);
  if (itr != this->pendingGets.end()) {
    PendingQuorumGet *pendingGet = itr->second;
    get_timeout_callback gtcb = pendingGet->gtcb;
    std::string key = pendingGet->key;
    this->pendingGets.erase(itr);
    delete pendingGet;
    gtcb(REPLY_TIMEOUT, key);
  }
}

/* Callback from a group replica on get operation completion. */
void ShardClient::HandleReadReply(const proto::ReadReply &reply) {
  auto itr = this->pendingGets.find(reply.req_id());
  if (itr == this->pendingGets.end()) {
    return; // this is a stale request
  }
  PendingQuorumGet *req = itr->second;
  Debug("[group %i] ReadReply for %lu.", group, reply.req_id());

  // value and timestamp are valid
  req->numReplies++;
  if (reply.has_committed()) {
    if (validateProofs) {
      std::string committedTxnDigest = TransactionDigest(reply.committed().proof().txn(), hashDigest);
      if (!ValidateTransactionWrite(reply.committed().proof(), committedTxnDigest,
            req->key,
            reply.committed().value(), reply.committed().timestamp(), config,
            signedMessages, keyManager)) {
        Debug("[group %i] Failed to validate committed value for read %lu.",
            group, reply.req_id());
        // invalid replies can be treated as if we never received a reply from
        //     a crashed replica
        return;
      }
    }

    Timestamp replyTs(reply.committed().timestamp());
    Debug("[group %i] ReadReply for %lu with committed %lu byte value and ts"
        " %lu.%lu.", group, reply.req_id(), reply.committed().value().length(),
        replyTs.getTimestamp(), replyTs.getID());
    if (req->firstCommittedReply || req->maxTs < replyTs) {
      req->maxTs = replyTs;
      req->maxValue = reply.committed().value();
    }
    req->firstCommittedReply = false;
  }

  proto::PreparedWrite prepared;
  bool hasPrepared = false;
  if (signedMessages) {
    if (reply.has_signed_prepared()) {
      if (ValidateSignedMessage(reply.signed_prepared(), keyManager, prepared)) {
        hasPrepared = true;
      } else {
        // TODO: should we continue with the committed value?
        return;
      }
    }
  } else {
    if (reply.has_prepared()) {
      hasPrepared = true;
      prepared = reply.prepared();
    }
  }

  if (hasPrepared) {
    Timestamp preparedTs(prepared.timestamp());
    Debug("[group %i] ReadReply for %lu with prepared %lu byte value and ts"
        " %lu.%lu.", group, reply.req_id(), prepared.value().length(),
        preparedTs.getTimestamp(), preparedTs.getID());
    auto preparedItr = req->prepared.find(preparedTs);
    if (preparedItr == req->prepared.end()) {
      req->prepared.insert(std::make_pair(preparedTs,
            std::make_pair(prepared, 1)));
    } else if (preparedItr->second.first == prepared) {
      preparedItr->second.second += 1;
    }

    if (signedMessages) {
      req->signedPrepared[preparedTs].push_back(reply.signed_prepared());
    }
  }

  if (req->numReplies >= req->rqs) {
    for (auto preparedItr = req->prepared.rbegin();
        preparedItr != req->prepared.rend(); ++preparedItr) {
      if (preparedItr->first < req->maxTs) {
        break;
      }

      if (preparedItr->second.second >= req->rds) {
        req->maxTs = preparedItr->first;
        req->maxValue = preparedItr->second.first.value();
        *req->dep.mutable_prepared() = preparedItr->second.first;
        if (signedMessages) {
          for (const auto &signedWrite : req->signedPrepared[preparedItr->first]) {
            *req->dep.mutable_proof()->mutable_signed_prepared()->add_msgs() =
                signedWrite;
          }
        } else {
          for (size_t i = 0; i < static_cast<size_t>(config->f) + 1; ++i) {
            *req->dep.mutable_proof()->mutable_prepared()->add_writes() =
                req->prepared[preparedItr->first].first;
          }
        }
        req->dep.set_involved_group(group);
        req->hasDep = true;
        break;
      }
    }
    pendingGets.erase(itr);
    ReadMessage *read = txn.add_read_set();
    *read->mutable_key() = req->key;
    req->maxTs.serialize(read->mutable_readtime());
    readValues[req->key] = req->maxValue;
    req->gcb(REPLY_OK, req->key, req->maxValue, req->maxTs, req->dep,
        req->hasDep, true);
    delete req;
  }
}

void ShardClient::HandlePhase1Reply(const proto::Phase1Reply &reply,
    const proto::SignedMessage &signedReply) {
  auto itr = this->pendingPhase1s.find(reply.req_id());
  if (itr == this->pendingPhase1s.end()) {
    return; // this is a stale request
  }

  PendingPhase1 *pendingPhase1 = itr->second;

  Debug("[group %i] PHASE1 callback ccr=%d", group, reply.ccr());
  
  if (!pendingPhase1->p1Validator.ProcessMessage(group, &reply,
        &signedReply)) {
    return;
  }

  if (signedMessages) {
    pendingPhase1->signedPhase1Replies[reply.ccr()].push_back(signedReply);
  }
  pendingPhase1->phase1Replies[reply.ccr()].push_back(reply);


  // We want:
  //   1) wait for 5f+1 commits if possible, even if we already have 3f+1 commits
  //        because fast path
  //   2) wait for 3f+1 commits if possible, instead of immediately aborting with
  //        f+1 abstains
  // need Phase1Validator to indicate whether its a fast commit, fast abort, or
  //   slow abort so that client can potentially wait for more commits if possible
  Phase1ValidationState state = pendingPhase1->p1Validator.GetState();
  switch (state) {
    case FAST_COMMIT:
      pendingPhase1->decision = proto::COMMIT;
      pendingPhase1->fast = true;
      Phase1Decision(itr);
      break;
    case FAST_ABORT:
      pendingPhase1->decision = proto::ABORT;
      pendingPhase1->fast = true;
      Phase1Decision(itr);
      break;
    case SLOW_COMMIT_FINAL:
      pendingPhase1->decision = proto::COMMIT;
      pendingPhase1->fast = false;
      Phase1Decision(itr);
      break;
    case SLOW_ABORT_FINAL:
      pendingPhase1->decision = proto::ABORT;
      pendingPhase1->fast = false;
      Phase1Decision(itr);
      break;
    case SLOW_COMMIT_TENTATIVE:
      if (!pendingPhase1->decisionTimeoutStarted) {
        uint64_t reqId = reply.req_id();
        pendingPhase1->decisionTimeout = new Timeout(transport,
            phase1DecisionTimeout, [this, reqId]() {
              auto itr = pendingPhase1s.find(reqId);
              if (itr == pendingPhase1s.end()) {
                return;
              }
              itr->second->decision = proto::COMMIT;
              itr->second->fast = false;
              Phase1Decision(itr);      
            }
          );
        pendingPhase1->decisionTimeout->Reset();
        pendingPhase1->decisionTimeoutStarted = true;
      }
      break;

    case SLOW_ABORT_TENTATIVE:
      if (!pendingPhase1->decisionTimeoutStarted) {
        uint64_t reqId = reply.req_id();
        pendingPhase1->decisionTimeout = new Timeout(transport,
            phase1DecisionTimeout, [this, reqId]() {
              auto itr = pendingPhase1s.find(reqId);
              if (itr == pendingPhase1s.end()) {
                return;
              }
              itr->second->decision = proto::ABORT;
              itr->second->fast = false;
              Phase1Decision(itr);      
            }
          );
        pendingPhase1->decisionTimeout->Reset();
        pendingPhase1->decisionTimeoutStarted = true;
      }
      break;
    case NOT_ENOUGH:
      break;
    default:
      break;
  }
}

void ShardClient::HandlePhase2Reply(const proto::Phase2Reply &reply,
    const proto::SignedMessage &signedReply) {
  auto itr = this->pendingPhase2s.find(reply.req_id());
  if (itr == this->pendingPhase2s.end()) {
    return; // this is a stale request
  }

  if (validateProofs) {
    itr->second->phase2Replies.push_back(reply);
    if (signedMessages) {
      itr->second->signedPhase2Replies.push_back(signedReply);
    }
  }

  if (reply.decision() == itr->second->decision) {
    itr->second->matchingReplies++;
  }

  if (itr->second->matchingReplies >= QuorumSize(config)) {
    PendingPhase2 *pendingPhase2 = itr->second;
    pendingPhase2->pcb(pendingPhase2->phase2Replies,
        pendingPhase2->signedPhase2Replies);
    this->pendingPhase2s.erase(itr);
    delete pendingPhase2;
  }
}

void ShardClient::Phase1Decision(uint64_t reqId) {
  auto itr = this->pendingPhase1s.find(reqId);
  if (itr == this->pendingPhase1s.end()) {
    return; // this is a stale request
  }

  Phase1Decision(itr);
}

void ShardClient::Phase1Decision(
    std::unordered_map<uint64_t, PendingPhase1 *>::iterator itr) {
  PendingPhase1 *pendingPhase1 = itr->second;
  pendingPhase1->pcb(pendingPhase1->decision, pendingPhase1->fast,
      pendingPhase1->phase1Replies, pendingPhase1->signedPhase1Replies);
  this->pendingPhase1s.erase(itr);
  delete pendingPhase1;
}

} // namespace indicus
