#include <ctime>
#include <chrono>

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

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
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

using namespace std;

struct user{
  string username;
  vector<string> is_following;
  vector<string> followed_by;
  vector<Message> new_posts;
  bool newposts = false;
};


vector<string> all_usersv;
vector<user> users;
mutex mu_;



void writeToTimeline(string name, Message &msg) {
  ofstream file;
  string message;
  time_t time = google::protobuf::util::TimeUtil::TimestampToTimeT(msg.timestamp());
  // string t_str(std::ctime(&time));
  // t_str[t_str.size()-1] = '\0';
  file.open(name + "timeline.txt", ios::app);
  if (file) {
    file << msg.username() << endl;
    file << time << endl;
    file << msg.msg();
  }
  file.close();
}

void saveClientState(grpc::string_ref name) {
  unique_lock<mutex> lock(mu_); 
  for (int i = 0; i < all_usersv.size(); ++i) { 
    if (all_usersv[i]== name) {
      all_usersv.erase(all_usersv.begin() + i);
    }
  }

  // update follower file with current followers
  ofstream file;
  for (int i = 0; i < users.size(); ++i) {
    if (users[i].username == name) {
      file.open(users[i].username  + "followers.txt");
      if (file) {
        for (int j = 0; j < users[i].followed_by.size(); ++j) {  
          file << users[i].followed_by[j];
        }
      }
      file.close();
    }
  }

  //update list of users this client follows with current list
  for (int i = 0; i < users.size(); ++i) {
    if (users[i].username == name) {
      file.open(users[i].username  + "following.txt");
      if (file) {
        for (int j = 0; j < users[i].is_following.size(); ++j) {  
          file << users[i].is_following[j];
        }
      }
      file.close();
    }
  }
}

user relog(string name) {   //create user struct and populate user struct
  
  user newuser;               
  newuser.username = name;     
  
  ifstream ifile;
  ifile.open(name + "followers.txt");
  if (ifile) {
    string user;
    while(getline(ifile, user)) {
      newuser.followed_by.push_back(user);
    }
    ifile.close();
  }
  else {
    ofstream newfile(name + "followers.txt");
    newfile.close();
  }

  ifile.open(name + "following.txt");
  if (ifile) {
    string user;
    while(getline(ifile, user)) {
      newuser.is_following.push_back(user);
    }
    ifile.close();
  }
  else {
    ofstream newfile(name + "following.txt");
    newfile.close();
  }

  newuser.newposts = false;
  return newuser;
}

void saveServerState() {
  //save users data structure to file
  ofstream file;
  string message;
  
  // copy current users
  file.open("users.txt");
  for (int i = 0; i < users.size(); ++i) {
    if (file) {
      file << users[i].username << endl;
    }
  }
  file.close();

  // copy current followers
  for (int i = 0; i < users.size(); ++i) {
    file.open(users[i].username  + "followers.txt");
    if (file) {
      for (int j = 0; j < users[i].followed_by.size(); ++j) {
        if (j == users[i].followed_by.size() - 1) {
          file << users[i].followed_by[j];
          break;
        }
        file << users[i].followed_by[j] << endl;
      }
    }
    file.close();
  }

  //copy list of users this client follows
  for (int i = 0; i < users.size(); ++i) {
    file.open(users[i].username  + "following.txt");
    if (file) {
      for (int j = 0; j < users[i].is_following.size(); ++j) {  
        if (j == users[i].is_following.size() - 1) {
          file << users[i].is_following[j];
          break;
        }
        file << users[i].is_following[j] << endl;
      }
    }
    file.close();
  }
}

void initServer() {
  //recreate the user struct vector from file data
  FILE* file;
  string fname = "users.txt";
  file = fopen(fname.c_str(), "r");
  if (file == NULL) {
    return;
  }
  unique_lock<mutex> lock(mu_);
  ifstream followers_file;
  ifstream following_file;
  ifstream userstream;
  string username;
  userstream.open("users.txt");
  while(!userstream.eof()) {
    user user;
    getline(userstream, username);
    user.username = username;
    followers_file.open(username  + "followers.txt");
    following_file.open(username  + "following.txt");
    while(!followers_file.eof()) {
      getline(followers_file, username);
      if (username != "") { user.followed_by.push_back(username);}
    }
    while(!following_file.eof()) {
      getline(following_file, username);
      if (username != "") { user.is_following.push_back(username);}
    }
    followers_file.close();
    following_file.close();
    users.push_back(user);
  }
  users.pop_back();
}

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
    //add all users to reply
    reply->mutable_all_users()->Add(all_usersv.begin(), all_usersv.end());
    
    // then add all users that user is following to reply
    for (int i = 0; i < users.size(); ++i) {
      if (users[i].username == request->username()) {
        for (int j = 0; j < users[i].followed_by.size(); ++j) {
          reply->add_following_users(users[i].followed_by[j]);
        }
      }
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

    //if we find user requested, add request user to that user's list of followers
    bool found = false;
    bool already = false;
    unique_lock<mutex> lock(mu_);
    
    for (int i = 0; i < users.size(); ++i) {
      if (request->arguments(0) == users[i].username) {
        for (int j = 0; j < users[i].followed_by.size(); ++j) {
          if (users[i].followed_by[j] == request->username()) {
            already = true;
          }
        }
        found = true;
        if (!already) {
          users[i].followed_by.push_back(request->username());
        }
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
        for (int i = 0; i < users.size(); ++i) {
          if (request->username() == users[i].username) {
            users[i].is_following.push_back(request->arguments(0));
          }
        }
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

    //if we find user requested, remove request user from that user's list of followers
    bool found = false;
    int idx;
    unique_lock<mutex> lock(mu_);
    for (int i = 0; i < users.size(); ++i) {
      if (request->arguments(0) == users[i].username) {
        for (int j = 0; j < users[i].followed_by.size(); ++j) {
          if (users[i].followed_by[j] == request->username()) {
            users[i].followed_by.erase(users[i].followed_by.begin() + j);
            found = true;
            break;
          }

        }
        // users.erase(users.begin() + idx);
      }
    }

    // if we found the requested user, romove their name from list of people client is following
    if (found) {
      for (int i = 0; i < users.size(); ++i) {
        if (request->username() == users[i].username) {
          users[i].is_following.erase(users[i].is_following.begin() + i);
        }
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
  
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // a new user and verify if the username is available
    // or already taken
    // ------------------------------------------------------------
    for (string user : all_usersv) {
      if (request->username() == user) {
        Status status = Status(grpc::StatusCode::ALREADY_EXISTS, NULL);
        return status;
        // cout << "already online" << endl;
        // return Status::CANCELLED;
      }
    } 
    unique_lock<mutex> lock(mu_);
    FILE* file;
    string fname = request->username() + "timeline.txt";
    file = fopen(fname.c_str(), "r");
    if (file != NULL) {
      all_usersv.push_back(request->username());  //add user to list of all users
    }
    else {
      ofstream newfile(request->username() + "timeline.txt");
      user newuser;
      bool found = false;
      for(int i = 0; i < users.size(); ++i) {         //check for user struct (if they just logged out it still exists = don't copy)
        if (users[i].username == request->username())
          found = true;
      }
      if (!found) {                                   //only recreate user struct from data if it does not exist
        newuser = relog(request->username());
        users.push_back(newuser);                   //add user to global vector of user structs
        newfile.close();
        all_usersv.push_back(request->username());  //add user to list of all users
      }
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
            for (int j = 0; j < users[i].followed_by.size(); ++j) {  //iterate through all users this one is followed by
              //write to users[i].followed_by[j]'s timeline file 
              writeToTimeline(users[i].followed_by[j], msg);
              for (int w = 0; w < users.size(); ++w) {               //find each of those users
                if (users[i].followed_by[j] == users[w].username) {  //add the new post to the followers list of new posts
                  users[w].new_posts.push_back(msg);
                  users[w].newposts = true;                          
                }
              }
            }
          }
        }
      }
    });


    thread clientWriter([&stream, uname] () { //launch writing thread
    
      Message test;
      Message msg;
      test.set_msg("");
      
      //-----if timeline file already exists then send up to 20 of the most recent posts ----
      ifstream file;
      string message;
      string name;
      string time;
      time_t t;
      Timestamp timestamp;
      // cout << users.size()
      vector<Message> msgvector;
      for (int i = 0; i < users.size(); ++i) {
        if (users[i].username == uname) {
          unique_lock<mutex> lock(mu_);
          file.open(users[i].username + "timeline.txt");
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
          users[i].new_posts.clear();
          users[i].newposts = false;
          file.close();
        }
      }

      // write new posts to the client if they have any
      while(true) {
        for (int i = 0; i < users.size(); ++i) {
          if (users[i].username == uname && users[i].newposts) {
            unique_lock<mutex> lock(mu_); 
            for (int j = 0; j < users[i].new_posts.size(); ++j) {
              stream->Write(users[i].new_posts[j]);
            }
            users[i].new_posts.clear();
            users[i].newposts = false;
          }
        }
        if (!stream->Write(test)) {
          break;
        }
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
  SNSServiceImpl service;
  ServerBuilder builder;

  builder.AddListeningPort(port_no, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  unique_ptr<Server> server(builder.BuildAndStart());
  initServer();
  saveServerState();
  cout << "Server listening on port: " << port_no << endl;
  server->Wait();
}

int main(int argc, char** argv) {
  
  std::string port = "0.0.0.0:50051";
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
          break;
      default:
	         std::cerr << "Invalid Command Line Argument\n";
    }
  }
  RunServer(port);
  return 0;
}
