#include "map-api/map-api-hub.h"

#include <iostream>
#include <fstream>
#include <memory>
#include <thread>

#include <glog/logging.h>

#include "core.pb.h"

#define FAKE_DISCOVERY "/tmp/mapapi-discovery.txt"

namespace map_api {

const char MapApiHub::kDiscovery[] = "map_api_hub_discovery";
MAP_API_MESSAGE_IMPOSE_STRING_MESSAGE(MapApiHub::kDiscovery);

/**
 * FIXME(tcies) the next two functions will need to go away!!
 */
std::weak_ptr<Peer> MapApiHub::ensure(const std::string& address) {
  return peers_.ensure(address);
}
void MapApiHub::getContextAndSocketType(
    zmq::context_t** context, int* socket_type) {
  *context = context_.get();
  *socket_type = ZMQ_REQ;
}

std::unordered_map<std::string,
std::function<void(const std::string&, Message*)> >
MapApiHub::handlers_;

MapApiHub::MapApiHub() : terminate_(false) {}

MapApiHub::~MapApiHub() {
  kill();
}

bool MapApiHub::init(const std::string &ipPort) {
  // FOR NOW: FAKE DISCOVERY

  registerHandler(kDiscovery, discoveryHandler);
  // 1. create own server
  context_ = std::unique_ptr<zmq::context_t>(new zmq::context_t());
  listenerConnected_ = false;
  listener_ = std::thread(listenThread, this, ipPort);
  {
    std::unique_lock<std::mutex> lock(condVarMutex_);
    listenerStatus_.wait(lock);
  }
  if (!listenerConnected_){
    context_.reset();
    return false;
  }

  // 2. connect to servers already on network (discovery from file)
  std::ifstream discovery(FAKE_DISCOVERY, std::ios::in);
  bool is_already_registered = false;
  peerLock_.writeLock();
  for (std::string other; getline(discovery,other);) {
    if (other.compare("") == 0) continue;
    if (other.compare(ipPort) == 0){
      LOG(INFO) << "Found registration of self from previous dead run, will "\
          "not register...";
      is_already_registered = true;
      continue;
    }
    LOG(INFO) << "Found peer " << other << ", connecting...";
    peers_.insert(other, *context_, ZMQ_REQ);
  }
  peerLock_.unlock();
  discovery.close();

  // 3. put own socket into discovery file
  if (!is_already_registered) {
    std::ofstream report(FAKE_DISCOVERY, std::ios::out | std::ios::app);
    report << ipPort << std::endl;
    report.close();
  }

  // 4. notify peers of self
  Message announce_self;
  announce_self.impose<kDiscovery>(ipPort);
  CHECK(peers_.undisputable_broadcast(announce_self));
  return true;
}

MapApiHub &MapApiHub::instance() {
  static MapApiHub instance;
  return instance;
}

void MapApiHub::kill() {
  if (terminate_){
    VLOG(3) << "Double termination";
    return;
  }
  // unbind and re-enter server
  terminate_ = true;
  listener_.join();
  // disconnect from peers
  peerLock_.writeLock();
  peers_.clear();
  peerLock_.unlock();
  // destroy context
  context_.reset();
  // clean discovery file
  // TODO(tcies) now only remove own registry
  std::ofstream cleanDiscovery(FAKE_DISCOVERY, std::ios::trunc);
}

int MapApiHub::peerSize() {
  int size;
  peerLock_.readLock();
  size = peers_.size();
  peerLock_.unlock();
  return size;
}

bool MapApiHub::registerHandler(
    const char* name,
    std::function<void(const std::string& serialized_type,
                       Message* response)> handler) {
  CHECK_NOTNULL(name);
  CHECK(handler);
  // TODO(tcies) div. error handling
  handlers_[name] = handler;
  return true;
}

void MapApiHub::request(
    const std::string& peer_address, const Message& request,
    Message* response) {
  peerLock_.readLock();
  peers_.request(peer_address, request, response);
  peerLock_.unlock();
}

void MapApiHub::broadcast(const Message& request,
                          std::unordered_map<std::string, Message>* responses) {
  peerLock_.readLock();
  peers_.broadcast(request, responses);
  peerLock_.unlock();
}

void MapApiHub::discoveryHandler(const std::string& peer, Message* response) {
  CHECK_NOTNULL(response);
  LOG(INFO) << "Peer " << peer << " requests discovery, let's "\
      "connect to it...";
  // lock peer set lock so we can write without a race condition
  instance().peerLock_.writeLock();
  instance().peers_.insert(peer, *instance().context_, ZMQ_REQ);
  instance().peerLock_.unlock();
  // ack by resend
  response->impose<Message::kAck>();
}

void MapApiHub::listenThread(MapApiHub *self, const std::string &ipPort) {
  zmq::socket_t server(*(self->context_), ZMQ_REP);
  {
    std::unique_lock<std::mutex> lock(self->condVarMutex_);
    // server only lives in this thread
    VLOG(3) << "Bind to " << ipPort;
    try {
      server.bind(("tcp://" + ipPort).c_str());
      self->listenerConnected_ = true;
      lock.unlock();
      self->listenerStatus_.notify_one();
    }
    catch (const std::exception &e) {
      LOG(ERROR) << "Server bind failed with exception \"" << e.what() <<
          "\", ipPort string was " << ipPort;
      self->listenerConnected_ = false;
      lock.unlock();
      self->listenerStatus_.notify_one();
      return;
    }
  }
  int timeOutMs = 100;
  server.setsockopt(ZMQ_RCVTIMEO, &timeOutMs, sizeof(timeOutMs));
  LOG(INFO) << "Server launched on " << ipPort;

  while (true) {
    zmq::message_t request;
    if (!server.recv(&request)) {
      //timeout, check if termination flag?
      if (self->terminate_)
        break;
      else
        continue;
    }
    proto::HubMessage query;
    query.ParseFromArray(request.data(), request.size());

    // Query handler
    std::unordered_map<std::string,
    std::function<void(const std::string&, Message*)> >::iterator handler =
        handlers_.find(query.type());
    CHECK(handlers_.end() != handler) << "Handler for message type " <<
        query.type() << " not registered";
    Message response;
    handler->second(query.serialized(), &response);
    std::string serialized_response = response.SerializeAsString();
    zmq::message_t response_message(serialized_response.size());
    memcpy((void *) response_message.data(), serialized_response.c_str(),
           serialized_response.size());
    server.send(response_message);
  }
  server.unbind(("tcp://" + ipPort).c_str());
  LOG(INFO) << "Listener terminated\n";
}

}
