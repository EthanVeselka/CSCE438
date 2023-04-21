#include <ctime>
#include <chrono>
#include <sys/stat.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include "sns.grpc.pb.h"
#include "snsCoordinator.grpc.pb.h"

#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity);

using google::protobuf::Timestamp;
using google::protobuf::Duration;
// using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;

using snsCoordinator::SNSCoordinator;
using snsCoordinator::ServerType;
using snsCoordinator::User;
using snsCoordinator::ClusterId;
using snsCoordinator::Server;
using snsCoordinator::Users;
using snsCoordinator::FollowSyncs;
using snsCoordinator::Heartbeat;

using namespace std;

struct user{
  string username;
  // vector<string> is_following;
  // vector<string> followed_by;
  vector<Message> new_posts;
  bool newposts = false;
};


vector<string> all_usersv;
vector<user> users;
mutex mu_;


string dir; // default directory for context files
string def = "0.0.0.0:";
string port = "9020";
string cip = "localhost";
string cp = "9000";
string id = "0";
string t;

unique_ptr<csce438::SNSService::Stub> servstub_;
unique_ptr<snsCoordinator::SNSCoordinator::Stub> coordstub_;

void writeToTimeline(string name, Message &msg) {
  ofstream file;
  string message;
  time_t time = google::protobuf::util::TimeUtil::TimestampToTimeT(msg.timestamp());
  file.open(dir + name + "timeline.txt", ios::app);
  if (file) {
    file << msg.username() << endl;
    file << time << endl;
    file << msg.msg();
  }
  file.close();

  file.open(dir + name + "newposts.txt", ios::app);
  if (file) {
    file << msg.username() << endl;
    file << time << endl;
    file << msg.msg();
  }
  file.close();
}

vector<Message> getMessages(string name) {
    vector<Message> posts;
    string message;
    string uname;
    string time;
    time_t t;
    Timestamp timestamp;
    Message msg;

    ifstream file;
    file.open(dir + name + "updates.txt");
    int count = 0;
    while(!file.eof()) {
        getline(file, uname);
        if (uname == "") {
            break;
        }
        getline(file, time);
        getline(file, message);

        msg.set_username(uname);
        msg.set_msg(message);

        stringstream ss(time);
        ss >> t;
        timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(t);
        
        msg.set_allocated_timestamp(&timestamp);
        posts.push_back(msg);
        msg.release_timestamp();
        ++count;
    }
    file.close();

    ofstream ofile;
    ofile.open(dir + name + "updates.txt");   // erase contents of file
    ofile.close();
    return posts;
}

void saveClientState(grpc::string_ref name) {
  unique_lock<mutex> lock(mu_); 
  for (int i = 0; i < all_usersv.size(); ++i) { 
    if (all_usersv[i] == name) {
      all_usersv.erase(all_usersv.begin() + i);
    }
  }
}


void saveServerState() {
  //save users data structure to file
  ofstream file;
  string message;
  
  // copy current users
  file.open(dir + "users.txt");
  for (int i = 0; i < users.size(); ++i) {
    if (file) {
      file << users[i].username << endl;
    }
  }
  file.close();

}

// void initServer() {

//   // FILE* file;
//   // string fname = dir + "users.txt";
//   // file = fopen(fname.c_str(), "r");
//   // if (file == NULL) {
//   //   return;
//   // }

//   // unique_lock<mutex> lock(mu_);
//   // //recreate the user struct vector from file data
//   // ifstream followers_file;
//   // ifstream following_file;
//   // ifstream userstream;
//   // string username;
//   // userstream.open(dir + "users.txt");
//   // while(!userstream.eof()) {
//   //   user user;
//   //   getline(userstream, username);
//   //   user.username = username;
//   //   users.push_back(user);
//   // }
//   // users.pop_back();

//   // ofstream ofile;
//   // for (int i = 0; i < users.size(); ++i) {
//   //   ofile.open(dir + users[i].username + "updates.txt");   // erase contents of file
//   //   ofile.close();
//   // }

// }

vector<string> populateLocal() {
  ifstream file;
  string user;
  vector<string> all;
  file.open(dir + "users.txt");
  while(!file.eof()) {
    getline(file, user);
    all.push_back(user);
    if (user == "") {break;}
  }
  all.pop_back();
  file.close();

  return all;
}

vector<string> populateAll() {
  ifstream file;
  string user;
  vector<string> all;
  file.open(dir + "allusers.txt");
  while(!file.eof()) {
    getline(file, user);
    all.push_back(user);
    if (user == "") {break;}
  }
  all.pop_back();
  file.close();

  return all;
}

vector<string> populateFollowers(string username) {
  ifstream file;
  string user;
  vector<string> followers;
  file.open(dir + username + "followers.txt");
  while(!file.eof()) {
    getline(file, user);
    followers.push_back(user);
    if (user == "") {break;}
  }
  followers.pop_back();
  file.close();

  return followers;
}

vector<string> populateFollowing(string username) {
  ifstream file;
  string user;
  vector<string> following;
  file.open(dir + username + "following.txt");
  while(!file.eof()) {
    getline(file, user);
    following.push_back(user);
    if (user == "") {break;}
  }
  following.pop_back();
  file.close();

  return following;
}

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
    //add all users to reply
    vector<string> all = populateAll();
    vector<string> followers = populateFollowers(request->username());
    for (int i = 0; i < all.size(); ++i) {
      reply->add_all_users(all[i]);
    }
    for (int i = 0; i < followers.size(); ++i) {
      reply->add_following_users(followers[i]);
    }
    reply->add_following_users(request->username());
    saveServerState();
    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to follow one of the existing
    // users
    // ------------------------------------------------------------

    //if this is master, forward call to secondary server
    if (t[0] == 'm') {
      grpc::ClientContext context;
      Request requestCopy;
      Reply replyCopy;
      requestCopy.CopyFrom(*request);

      //contact coordinator first then connect to the server cluster as done here
      grpc::Status status = servstub_->Follow(&context, requestCopy, &replyCopy);
    }

    //if we find user requested, add request user to that user's list of followers
    bool found = false;
    bool already = false;
    unique_lock<mutex> lock(mu_);
    
    vector<string> all = populateAll();
    vector<string> already_following = populateFollowing(request->username());

    for (int i = 0; i < all.size(); ++i) {
      if (all[i] == request->arguments(0)) {
        for (int j = 0; j < already_following.size(); ++j) {
          if (already_following[j] == request->arguments(0)) {
            already = true;
          }
        }
        found = true;
      }
    }

    // if we found the requested user, add their name to list of people client is following
    if (found) {
      if (already) {
        saveServerState();
        Status status = Status(grpc::StatusCode::ALREADY_EXISTS, "");
        return status;
      }
      else {
        ofstream file;
        file.open(dir + request->username() + "following.txt", ios::app);
        file << request->arguments(0) << endl;
        file.close();

      }
      saveServerState();
      return Status::OK;
    }
    else {
      saveServerState();
      Status status = Status(grpc::StatusCode::NOT_FOUND, "");
      return status;
    }
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to unfollow one of his/her existing
    // followers
    // ------------------------------------------------------------

    //if this is master, forward call to secondary server
    if (t[0] == 'm') {
      grpc::ClientContext context;
      Request requestCopy;
      Reply replyCopy;
      requestCopy.CopyFrom(*request);

      //contact coordinator first then connect to the server cluster as done here
      grpc::Status status = servstub_->UnFollow(&context, requestCopy, &replyCopy);
    }

    //if we find user requested, remove request user from that user's list of followers
    bool found = false;
    bool already = false;
    unique_lock<mutex> lock(mu_);
    
    vector<string> following = populateFollowing(request->username());
    vector<string> followers = populateFollowers(request->arguments(0));

    for (int i = 0; i < following.size(); ++i) {
      if (following[i] == request->arguments(0)) {
        found = true;
      }
    }

    // if we found the requested user, remove their name from list of people client is following
    if (found) {
      ofstream file;
      file.open(dir + request->username() + "following.txt");
      for (int i = 0; i < following.size(); ++i) {
        if (following[i] == request->arguments(0)) { continue;}
        file << following[i] << endl;
      }
      file.close();

      saveServerState();
      return Status::OK;
    }
    else {
      saveServerState();
      Status status = Status(grpc::StatusCode::NOT_FOUND, "");
      return status;
    }
  }
  
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // a new user and verify if the username is available
    // or already taken
    // ------------------------------------------------------------
    
    //if this is master, forward call to secondary server

    unique_lock<mutex> lock(mu_);
    Status status;
    bool l = false;
    bool global = false;
    vector<string> all = populateAll();
    
    for (string user : all) {
      if (request->username() == user) {
        status = Status(grpc::StatusCode::ALREADY_EXISTS, NULL);
        global = true;
      }
    }

    if (t[1] = 'l') {
        vector<string> local = populateLocal();
        for (int i = 0; i < local.size(); ++i) {
          if (local[i] == request->username()) {
            l = true;
          }
        }
    }

    if (global && !l) {
      return status;
    }
    else {
      ofstream file;
      file.open(dir + "users.txt", ios::app);
      file << request->username() << endl;
      file.close();

      user newuser;
      newuser.username = request->username();     //only recreate user struct from data if it does not exist
      users.push_back(newuser);                   //add user to global vector of user structs
      all_usersv.push_back(request->username());
    }


    FILE* file;
    string fname = dir + request->username() + "timeline.txt";
    file = fopen(fname.c_str(), "r");
    if (file != NULL) {
      all_usersv.push_back(request->username());  //add user to local list of all users if new to server
    }
    else {
      ofstream newfile(dir + request->username() + "timeline.txt");
      user newuser;
      bool found = false;
      for(int i = 0; i < users.size(); ++i) {         //check for user struct (if they just logged out it still exists = don't copy)
        if (users[i].username == request->username())
          found = true;
      }
      if (!found) {  
        user newuser;
        newuser.username = request->username();     //only recreate user struct from data if it does not exist
        users.push_back(newuser);                   //add user to global vector of user structs
        newfile.close();
        all_usersv.push_back(request->username());  //add user to list of all users
      }
    }


    if (t[0] == 'm') {
      grpc::ClientContext coordcontext;
      grpc::ClientContext context;
      Request requestCopy;
      Reply replyCopy;
      requestCopy.CopyFrom(*request);
    
      if (servstub_ == NULL) {
        ClusterId cluster;
        cluster.set_cluster(stoi(id));
        Server s;
        grpc::Status status1 = coordstub_->GetSlave(&coordcontext, cluster, &s);
        servstub_ = SNSService::NewStub(CreateChannel(s.server_ip() + ":" + s.port_num(), grpc::InsecureChannelCredentials()));
      }

      grpc::Status status2 = servstub_->Login(&context, requestCopy, &replyCopy);
    }

    
    return Status::OK;
  }

  Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // receiving a message/post from a user, recording it in a file
    // and then making it available on his/her follower's streams
    // ------------------------------------------------------------
    
    //add new stream object to list of following streams for each user this user follows
    std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto auth = metadata.find("user");
    auto uname = auth->second;
    
    //enter loop reading/writing messages
    thread clientReader([&stream, uname] () { //thread reads incoming messages, updates files and newpost objects
      Message msg;
      while(stream->Read(&msg)) {
        unique_lock<mutex> lock(mu_);
        for (int i = 0; i < users.size(); ++i) {     //iterate through users
          if (msg.username() == users[i].username) { 
            //write to user[i]'s timeline file
            writeToTimeline(users[i].username, msg);
          }
        }
      }
    });

    //writes updates from each timeline to their respective users.
    thread clientWriter([&stream, uname] () { //launch write-from-timeline-to-client thread
    
      Message test;
      Message msg;
      test.set_msg("");
      
      //-----if timeline file already exists then send up to 20 of the most recent posts ----
      ifstream file;
      string message;
      string name;
      string n;
      string time;
      time_t t;
      Timestamp timestamp;
      // cout << users.size()
      vector<Message> msgvector;
      for (int i = 0; i < users.size(); ++i) {
        if (users[i].username == uname) {
          n = users[i].username;
          unique_lock<mutex> lock(mu_);
          file.open(dir + users[i].username + "timeline.txt");
          if (file) {
            int count = 0;
            while (!file.eof()) {
              //parse lines into msg objects, put in vector for reverse iteration
              getline(file, name);
              if (name == "") {
                break;
              }
              getline(file, time);
              getline(file, message);

              msg.set_username(name);
              msg.set_msg(message);

              stringstream ss(time);
              ss >> t;
              timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(t);
              
              msg.set_allocated_timestamp(&timestamp); 
              msgvector.push_back(msg);
              msg.release_timestamp();
              ++count;
            }
            //stream messages in reverse
            int steps = (count >= 20) ? 20 : count;
            for (int j = 0; j < steps; ++j) {
              stream->Write(msgvector.at(msgvector.size() - 1));
              msgvector.pop_back();
            }
          }
          file.close();
        }
      }

      // write new posts to the client if they have any
      // while(true) {
      //   for (int i = 0; i < users.size(); ++i) {
      //     if (users[i].username == uname && users[i].newposts) {
      //       unique_lock<mutex> lock(mu_); 
      //       for (int j = 0; j < users[i].new_posts.size(); ++j) {
      //         stream->Write(users[i].new_posts[j]);
      //       }
      //       users[i].new_posts.clear();
      //       users[i].newposts = false;
      //     }
      //   }
      //   if (!stream->Write(test)) {
      //     break;
      //   }
      // }
      ofstream ofile;
      ofile.open(dir + n + "updates.txt");   // erase contents of file
      ofile.close();
        
      while(true) {
        for (int i = 0; i < users.size(); ++i) {
          if (users[i].username == uname) {
            unique_lock<mutex> lock(mu_);
            vector<Message> messages = getMessages(users[i].username);
            for (int j = 0; j < messages.size(); ++j) {
              Message m;
              m.CopyFrom(messages[j]);
              stream->Write(m);
            }
          }
        }
        if (!stream->Write(test)) {
          break;
        }
        sleep(1);
      }
    });
    
    clientReader.join();
    clientWriter.join();
    
    
    saveClientState(uname);
    // deletes user from list of user so they can log back in, if disconnected, maintains follow/followed list

    return Status::OK;
  }

};

void RunServer(std::string port_no) {
  // ------------------------------------------------------------
  // In this function, you are to write code 
  // which would start the server, make it listen on a particular
  // port number.
  // ------------------------------------------------------------
  if (t[0] == 'm') {
    dir = "master_" + id + "/";
    mkdir(("master_" + id).c_str(), 0777);
  }
  else if (t[1] == 'l') {
    dir = "slave_" + id + "/";
    mkdir(("slave_" + id).c_str(), 0777);
  }

  SNSServiceImpl service;
  ServerBuilder builder;

  builder.AddListeningPort(port_no, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  unique_ptr<grpc::Server> server(builder.BuildAndStart());
  // initServer();
  saveServerState();
  cout << "Server listening on port: " << port_no << endl;

  // register with coordinator and get slave server
  coordstub_ = SNSCoordinator::NewStub(CreateChannel(cip + ":" + cp, grpc::InsecureChannelCredentials()));
  
  grpc::ClientContext coordcontext;
  std::unique_ptr<grpc::ClientReaderWriter<Heartbeat, Heartbeat>> stream(coordstub_->HandleHeartBeats(&coordcontext));
  
  Heartbeat ping;
  ServerType st;
  time_t time;

  if (t[0] == 'm') {st = ServerType::MASTER;}
  else if (t[1] == 'l') {st = ServerType::SLAVE;}

  ping.set_server_id(stoi(id));
  ping.set_server_ip(cip);
  ping.set_server_port(port);
  ping.set_server_type(st);
  auto const now = chrono::system_clock::now();
  time = chrono::system_clock::to_time_t(now);
  Timestamp t = google::protobuf::util::TimeUtil::TimeTToTimestamp(time);
  ping.set_allocated_timestamp(&t);
  stream->Write(ping);
  ping.release_timestamp();
  
  thread writer([&stream] () {
      // unique_lock<mutex> lock(mu_);
      time_t time;
      Heartbeat ping;
      while(stream->Read(&ping)) {
        auto const now = chrono::system_clock::now();
        time = chrono::system_clock::to_time_t(now);
        Timestamp ts = google::protobuf::util::TimeUtil::TimeTToTimestamp(time);
        ping.set_allocated_timestamp(&ts);
        if (!stream->Write(ping)) {
          break;
        }
        ping.release_timestamp();
      }
  });
  writer.detach();
  server->Wait();
}

int main(int argc, char** argv) {
  // 3010 default port no, or 10000

  for (int i = 1; i < argc; ++i) {
    if (argv[i] == string("-cip") && i + 1 < argc) { cip = argv[++i];}
    else if (argv[i] == string("-cp") && i + 1 < argc) { cp = argv[++i];}
    else if (argv[i] == string("-p") && i + 1 < argc) { port = argv[++i];}
    else if (argv[i] == string("-id") && i + 1 < argc) { id = argv[++i];}
    else if (argv[i] == string("-t") && i + 1 < argc) { t = argv[++i];}
    else {cerr << "Invalid Command Line Argument\n";}
  }

  RunServer(def + port);
  // contact coordinator and register server
  return 0;
}
