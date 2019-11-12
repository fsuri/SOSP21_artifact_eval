#include "store/mortystore/branch_generator.h"

#include <sstream>

#include "lib/message.h"
#include "store/mortystore/common.h"

#include <google/protobuf/util/message_differencer.h>

namespace mortystore {

BranchGenerator::BranchGenerator() {
  _Latency_Init(&generateLatency, "branch_generation");
}

BranchGenerator::~BranchGenerator() {
  //Latency_Dump(&generateLatency);
}

void BranchGenerator::AddPendingWrite(const std::string &key,
    const proto::Branch &branch) {
  pending_writes[key].insert(branch);
}

void BranchGenerator::AddPendingRead(const std::string &key,
    const proto::Branch &branch) {
  pending_reads[key].insert(branch);
}

void BranchGenerator::ClearPending(uint64_t txn_id) {
  for (auto itr = pending_reads.begin(); itr != pending_reads.end(); ++itr) {
    for (auto jtr = itr->second.begin(); jtr != itr->second.end(); ) {
      if (jtr->id() == txn_id) {
        jtr = itr->second.erase(jtr);
      } else {
        ++jtr;
      }
    }
  }
  for (auto itr = pending_writes.begin(); itr != pending_writes.end(); ++itr) {
    for (auto jtr = itr->second.begin(); jtr != itr->second.end(); ) {
      if (jtr->id() == txn_id) {
        jtr = itr->second.erase(jtr);
      } else {
        ++jtr;
      }
    }
  }
  for (auto itr = already_generated.begin(); itr != already_generated.end(); ) {
    if (itr->id() == txn_id) {
      itr = already_generated.erase(itr);
    } else {
      ++itr;
    }
  }
}

void BranchGenerator::GenerateBranches(const proto::Branch &init,
    proto::OperationType type, const std::string &key,
    const std::vector<proto::Transaction> &committed,
    std::vector<proto::Branch> &new_branches) {
  Latency_Start(&generateLatency);

  std::vector<proto::Branch> generated_branches;
  std::unordered_map<uint64_t, std::unordered_set<proto::Branch, BranchHasher, BranchComparer>> pending_branches;
  pending_branches[init.txn().id()].insert(init);
  if (type == proto::OperationType::WRITE) {
    for (auto b : pending_writes[key]) {
      pending_branches[b.txn().id()].insert(b);
    }
    for (auto b : pending_reads[key]) {
      pending_branches[b.txn().id()].insert(b);
    }

  } else {
    for (auto b : pending_writes[key]) {
      pending_branches[b.txn().id()].insert(b);
    }
  }
  std::vector<uint64_t> txns_list;
  for (auto kv : pending_branches) {
   txns_list.push_back(kv.first); 
  }

  if (Message_DebugEnabled(__FILE__)) {
    std::stringstream ss;
    ss << "Committed: ";
    PrintTransactionList(committed, ss);
    Debug("%s", ss.str().c_str());
  }

  GenerateBranchesSubsets(pending_branches, txns_list, committed, new_branches);
  
  Latency_End(&generateLatency);
}

void BranchGenerator::GenerateBranchesSubsets(
    const std::unordered_map<uint64_t, std::unordered_set<proto::Branch, BranchHasher, BranchComparer>> &pending_branches,
    const std::vector<uint64_t> &txns, const std::vector<proto::Transaction> &committed,
    std::vector<proto::Branch> &new_branches, std::vector<uint64_t> subset,
    int64_t i) {
  if (subset.size() > 0) {
    GenerateBranchesPermutations(pending_branches, subset, committed, new_branches);
  }

  for (size_t j = i + 1; j < txns.size(); ++j) {
    subset.push_back(txns[j]);

    GenerateBranchesSubsets(pending_branches, txns, committed, new_branches, subset, j);

    subset.pop_back();
  }
}

void BranchGenerator::GenerateBranchesPermutations(
    const std::unordered_map<uint64_t, std::unordered_set<proto::Branch, BranchHasher, BranchComparer>> &pending_branches,
    const std::vector<uint64_t> &txns, const std::vector<proto::Transaction> &committed,
    std::vector<proto::Branch> &new_branches) {
  std::vector<uint64_t> txns_sorted(txns);
  std::sort(txns_sorted.begin(), txns_sorted.end());
  do {

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Permutation: [";
      for (size_t i = 0; i < txns_sorted.size(); ++i) {
        ss << txns_sorted[i];
        if (i < txns_sorted.size() - 1) {
          ss << ", ";
        }
      }
      ss << "]";
      Debug("%s", ss.str().c_str());
    }
    std::vector<std::vector<proto::Transaction>> new_seqs;
    new_seqs.push_back(committed);

    for (size_t i = 0; i < txns_sorted.size() - 1; ++i) {
      std::vector<std::vector<proto::Transaction>> new_seqs1;
      for (size_t j = 0; j < new_seqs.size(); ++j) {
        auto itr = pending_branches.find(txns_sorted[i]);
        if (itr != pending_branches.end()) {
          for (const proto::Branch &branch : itr->second) {
            if (branch.txn().ops().size() == 1 || WaitCompatible(branch, new_seqs[j])) {
              std::vector<proto::Transaction> seq(new_seqs[j]);
              seq.push_back(branch.txn());
              new_seqs1.push_back(seq);
            }
          }
        }
      }
      new_seqs.insert(new_seqs.end(), new_seqs1.begin(), new_seqs1.end());
    }
    auto itr = pending_branches.find(txns_sorted[txns_sorted.size() - 1]);
    if (itr != pending_branches.end()) {
      for (const proto::Branch &branch : itr->second) {
        proto::Branch prev(branch);
        prev.mutable_txn()->mutable_ops()->RemoveLast();
        if (Message_DebugEnabled(__FILE__)) {
          std::stringstream ss;
          ss << "  Potential: ";
          PrintBranch(branch, ss);
          Debug("%s", ss.str().c_str());

          ss.str("");

          ss << "  Prev: ";
          PrintBranch(prev, ss);
          Debug("%s", ss.str().c_str());
        }
        for (const std::vector<proto::Transaction> &seq : new_seqs) {
          if (Message_DebugEnabled(__FILE__)) {
            std::stringstream ss;
            ss << "  Seq: ";
            PrintTransactionList(seq, ss);
            Debug("%s", ss.str().c_str());
          }
          if (WaitCompatible(prev, seq)) {
            Debug("  Compatible");
            proto::Branch new_branch(branch); 
            new_branch.clear_deps();
            for (const proto::Operation &op : new_branch.txn().ops()) {
              proto::Transaction t;
              if (MostRecentConflict(op, seq, t)) {
                if (std::find_if(new_branch.deps().begin(), new_branch.deps().end(),
                  [&](const proto::Transaction &other) {
                    return t == other;
                  }) == new_branch.deps().end()) {
                    proto::Transaction *tseq = new_branch.add_deps();
                    *tseq = t;
                  }
              }
            }
            if (Message_DebugEnabled(__FILE__)) {
              std::stringstream ss;
              ss << "    Generated branch: ";
              PrintBranch(new_branch, ss);
              Debug("%s", ss.str().c_str());
            }
            std::cerr << "ag length: " << already_generated.size() << std::endl;
            if (already_generated.find(new_branch) == already_generated.end()) {
              new_branches.push_back(new_branch);
              already_generated.insert(new_branch);
            }
          }
        }
      }
    }
  } while (std::next_permutation(txns_sorted.begin(), txns_sorted.end()));
}

} /* mortystore */
