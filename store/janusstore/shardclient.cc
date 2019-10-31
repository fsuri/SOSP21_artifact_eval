// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
#include "store/janusstore/shardclient.h"

namespace janusstore {

using namespace std;
using namespace proto;

ShardClient::ShardClient(transport::Configuration *config, Transport *transport,
	uint64_t client_id, int shard, int closestReplica)
	: client_id(client_id), transport(transport), config(config), shard(shard),
  responded(0) {

  this->num_replicas = config->n;
  printf("shardclient%d has %d replicas\n", shard, this->num_replicas);

  client = new replication::ir::IRClient(*config, transport, client_id);

  if (closestReplica == -1) {
    replica = client_id % config->n;
  } else {
    replica = closestReplica;
  }
  Debug("Sending unlogged to replica %i", replica);
}

ShardClient::~ShardClient() {
}

void ShardClient::PreAccept(const Transaction &txn, uint64_t ballot, client_preaccept_callback pcb) {
	Debug("[shard %i] Sending PREACCEPT for txn_id %i [%llu]", shard, txn.getTransactionId(), client_id);

	std::vector<janusstore::proto::Reply> replies;
	uint64_t txn_id = txn.getTransactionId();

	PendingRequest *req = new PendingRequest(txn_id);
    pendingReqs[txn_id] = req;
    req->cpcb = pcb;
    req->preaccept_replies = {};

	/* TODO deprecate */
	pair<uint64_t, std::vector<janusstore::proto::Reply>> entry (txn_id, replies);
	preaccept_replies.insert(entry);

	// create PREACCEPT Request
	string request_str;
	Request request;
	// PreAcceptMessage payload;
	request.set_op(Request::PREACCEPT);

	// serialize a Transaction into a TransactionMessage
	janusstore::proto::TransactionMessage txn_msg;
	txn.serialize(&txn_msg, this->shard);
	// printf("%s", txn_msg->DebugString().c_str());
	request.mutable_preaccept()->set_allocated_txn(&txn_msg);
	// txn.serialize(request.mutable_preaccept()->mutable_txn());
	request.mutable_preaccept()->set_ballot(ballot);


	// now we can serialize the request and send it to replicas
	request.SerializeToString(&request_str);
	request.mutable_preaccept()->release_txn();

	// store callback with txnid in a map for the preaccept cb
	// because we can't pass it into a continuation function
	/* TODO deprecate */
	pair<uint64_t, client_preaccept_callback> callback_entry (txn_id, pcb);
	this->pcb_map.insert(callback_entry);

	// ShardClient continutation will be able to invoke
	// the Client's callback function when all responses returned
	Debug("shardclient%d shardcasting PREACCEPT to %d replicas\n for txn %i",
		this->shard, this->num_replicas, txn_id);
	for (int i = 0; i < this->num_replicas; i++) {
		client->InvokeUnlogged(shard, i, request_str,
			std::bind(&ShardClient::PreAcceptContinuation, this,
			placeholders::_1, placeholders::_2), nullptr, 10000);
	}
}

void ShardClient::Accept(uint64_t txn_id, std::vector<uint64_t> deps, uint64_t ballot, client_accept_callback acb) {
	Debug("[shard %i] Sending ACCEPT [%llu]", shard, client_id);

	PendingRequest* req = this->pendingReqs[txn_id];
	req->accept_replies = {};
	req->cacb = acb;

	/* TODO deprecate */
	std::vector<janusstore::proto::Reply> replies;
	pair<uint64_t, std::vector<janusstore::proto::Reply>> entry (txn_id, replies);
	accept_replies.insert(entry);

	// create ACCEPT Request
	string request_str;
	Request request;
	request.set_op(Request::ACCEPT);

	// AcceptMessage payload
	request.mutable_accept()->set_txnid(txn_id);
	request.mutable_accept()->set_ballot(ballot);
	for (auto dep : deps) {
		request.mutable_accept()->mutable_dep()->add_txnid(dep);
	}

	request.SerializeToString(&request_str);

	// store callback with txnid in a map for the preaccept cb
	// because we can't pass it into a continuation function
	/* TODO deprecate */
	pair<uint64_t, client_accept_callback> callback_entry (txn_id, acb);
	this->acb_map.insert(callback_entry);

	// ShardClient continutation will be able to invoke
	// the Client's callback function when all responses returned
	for (int i = 0; i < this->num_replicas; i++) {
		client->InvokeUnlogged(shard, i, request_str,
			std::bind(&ShardClient::AcceptContinuation, this,
			placeholders::_1, placeholders::_2), nullptr, 10000);
	}
}

void ShardClient::Commit(uint64_t txn_id, std::vector<uint64_t> deps, client_commit_callback ccb) {
	Debug("[shard %i] Sending COMMIT for txn %llu", shard, txn_id);

	PendingRequest* req = this->pendingReqs[txn_id];
	req->commit_replies = {};
	req->cccb = ccb;

	/* TODO deprecate */
	std::vector<janusstore::proto::Reply> replies;
	pair<uint64_t, std::vector<janusstore::proto::Reply>> entry (txn_id, replies);
	commit_replies.insert(entry);

	// create COMMIT Request
	string request_str;
	Request request;
	request.set_op(Request::COMMIT);
	// CommitMessage payload
	request.mutable_commit()->set_txnid(txn_id);
	for (auto dep : deps) {
		request.mutable_commit()->mutable_dep()->add_txnid(dep);
	}

	request.SerializeToString(&request_str);

	// store callback with txnid in a map for the preaccept cb
	// because we can't pass it into a continuation function
	/* TODO deprecate */
	pair<uint64_t, client_commit_callback> callback_entry (txn_id, ccb);
	this->ccb_map.insert(callback_entry);

	Debug("[shard %i] adding ccb for %i, %llu, %llu", shard, txn_id, ccb_map.size(), client_id);

	// ShardClient continutation will be able to invoke
	// the Client's callback function when all responses returned
	for (int i = 0; i < this->num_replicas; i++) {
		client->InvokeUnlogged(shard, i, request_str,
			std::bind(&ShardClient::CommitContinuation, this,
			placeholders::_1, placeholders::_2), nullptr, 10000);
	}
}

void ShardClient::PreAcceptCallback(uint64_t txn_id, janusstore::proto::Reply reply, client_preaccept_callback pcb) {

	PendingRequest* req = this->pendingReqs[txn_id];
	req->responded++;

	this->responded++;

    printf("%s\n", ("SHARDCLIENT" + to_string(this->shard) + " - PREACCEPT CB - txn " + to_string(txn_id)).c_str());

	// aggregate replies for this transaction
	if (this->preaccept_replies.count(txn_id)) {
		// key already exists, so append to list
		std::vector<janusstore::proto::Reply> *list;
		list = &this->preaccept_replies.at(txn_id);
		list->push_back(reply);
	} else {
		this->preaccept_replies[txn_id] = std::vector<janusstore::proto::Reply>({reply});
	}

	if (this->responded == this->num_replicas) {
	    printf("%s\n", ("SHARDCLIENT" + to_string(this->shard) + " - REPLICAS RESPONDED FOR - txn " + to_string(txn_id)).c_str());

	    req->responded = 0;
		this->responded = 0;
		pcb(shard, this->preaccept_replies[txn_id]);
	}
}

void ShardClient::AcceptCallback(uint64_t txn_id, janusstore::proto::Reply reply, client_accept_callback acb) {

	PendingRequest* req = this->pendingReqs[txn_id];
	req->responded++;

	this->responded++;

    printf("%s\n", ("SHARDCLIENT" + to_string(this->shard) + " - ACCEPT CB - txn " + to_string(txn_id)).c_str());

	// aggregate replies for this transaction
	if (this->accept_replies.count(txn_id)) {
		// key already exists, so append to list
		std::vector<janusstore::proto::Reply> *list;
		list = &this->accept_replies.at(txn_id);
		list->push_back(reply);
	} else {
		this->accept_replies[txn_id] = std::vector<janusstore::proto::Reply>({reply});
	}

	if (this->responded == this->num_replicas) {
		req->responded = 0;
		this->responded = 0;
		acb(shard, this->accept_replies[txn_id]);
	}
}

void ShardClient::CommitCallback(uint64_t txn_id, janusstore::proto::Reply reply, client_commit_callback ccb) {

	PendingRequest* req = this->pendingReqs[txn_id];
	req->responded++;

	this->responded++;

    // printf("%s\n", ("SHARDCLIENT" + to_string(this->shard) + " - COMMIT CB - txn " + to_string(txn_id)).c_str());
    Debug("In shardclient CommitCallback");

	// aggregate replies for this transaction
	if (this->commit_replies.count(txn_id)) {
		// key already exists, so append to list
		std::vector<janusstore::proto::Reply> *list;
		list = &this->commit_replies.at(txn_id);
		list->push_back(reply);
	} else {
		this->commit_replies[txn_id] = std::vector<janusstore::proto::Reply>({reply});
	}

	if (this->responded == this->num_replicas) {
		req->responded = 0;
		this->responded = 0;
		ccb(shard, this->commit_replies[txn_id]);
	}
}

void ShardClient::PreAcceptContinuation(const string &request_str, const string &reply_str) {

	Debug("In preaccept continuation");
	janusstore::proto::Reply reply;
	reply.ParseFromString(reply_str);
	uint64_t txn_id = NULL;

	if (reply.op() == janusstore::proto::Reply::PREACCEPT_OK) {
		txn_id = reply.preaccept_ok().txnid();
	} else if (reply.op() == janusstore::proto::Reply::PREACCEPT_NOT_OK) {
		txn_id = reply.preaccept_not_ok().txnid();
	} else {
		Panic("Not a preaccept reply");
	}

	// get the pcb
  	client_preaccept_callback pcb = this->pcb_map.at(txn_id);
  	// invoke the shardclient callback
  	this->PreAcceptCallback(txn_id, reply, pcb);
}

void ShardClient::AcceptContinuation(const string &request_str,
    const string &reply_str) {

	Debug("In accept continuation");
	janusstore::proto::Reply reply;
  	reply.ParseFromString(reply_str);
	uint64_t txn_id = NULL;

	if (reply.op() == janusstore::proto::Reply::ACCEPT_OK) {
		txn_id = reply.accept_ok().txnid();
	} else if (reply.op() == janusstore::proto::Reply::ACCEPT_NOT_OK) {
		txn_id = reply.accept_not_ok().txnid();
	} else {
		Panic("Not an accept reply");
	}
  	// get the acb
  	client_accept_callback acb = this->acb_map.at(txn_id);
  	// invoke the shardclient callback
  	this->AcceptCallback(txn_id, reply, acb);
}

void ShardClient::CommitContinuation(const string &request_str,
    const string &reply_str) {

	janusstore::proto::Reply reply;
  	reply.ParseFromString(reply_str);
	uint64_t txn_id = NULL;

	if (reply.op() == janusstore::proto::Reply::COMMIT_OK) {
		txn_id = reply.commit_ok().txnid();
	} else {
		Panic("Not a commit reply");
	}

	Debug("[shard %i] In commit continuation for txn %i, %i", shard, txn_id, ccb_map.size());
	// get the ccb
	client_commit_callback ccb = ccb_map[txn_id];
	// invoke the shardclient callback
	this->CommitCallback(txn_id, reply, ccb);
}
}
