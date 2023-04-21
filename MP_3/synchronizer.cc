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
#include "snsFollowSync.grpc.pb.h"
#include "time.h"

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

using csce438::SNSService;
using csce438::Message;
using csce438::Request;
// using csce438::Reply;

using snsCoordinator::SNSCoordinator;
using snsCoordinator::ServerType;
using snsCoordinator::User;
using snsCoordinator::ClusterId;
using snsCoordinator::Server;
// using snsCoordinator::Users;
using snsCoordinator::FollowSyncs;
using snsCoordinator::Heartbeat;

using snsFollowSync::SNSFollowSync;
using snsFollowSync::Relation;
using snsFollowSync::Users;
using snsFollowSync::Post;
using snsFollowSync::Reply;


using namespace std;

int hb = 1;


struct syncron{
    string ip;
    string port;
};

vector<int> local_users;
vector<int> all_users;
vector<Relation> relations;
FollowSyncs syncs;

map<int, syncron> user_syncs;


mutex mu_;

string sl_dir;
string ma_dir;
string dir;
string def = "0.0.0.0:";
string port = "9010";
string cip = "localhost";
string cp = "9000";
string id = "0";

unique_ptr<snsFollowSync::SNSFollowSync::Stub> syncstub_;
unique_ptr<snsCoordinator::SNSCoordinator::Stub> coordstub_;

// string name, string msg, Timestamp t
void writeToTimeline(Post post) {
    // Post post;
    unique_lock<mutex> lock(mu_);
    ofstream file;
    string message;
    time_t time = google::protobuf::util::TimeUtil::TimestampToTimeT(post.timestamp());
    file.open(ma_dir + to_string(post.follower_user()) + "timeline.txt", ios::app); //write to master copy
    if (file) {
        file << post.posted_by() << endl;
        file << time << endl;
        file << post.msg() << endl;
    }
    file.close();

    file.open(sl_dir + to_string(post.follower_user()) + "timeline.txt", ios::app); //write to slave copy
    if (file) {
        file << post.posted_by() << endl;
        file << time << endl;
        file << post.msg() << endl;
    }
    file.close();

    file.open(ma_dir + to_string(post.follower_user()) + "updates.txt", ios::app);  //write to master copy
    if (file) {
        file << post.posted_by() << endl;
        file << time << endl;
        file << post.msg() << endl;
    }
    file.close();

    file.open(sl_dir + to_string(post.follower_user()) + "updates.txt", ios::app); //write to slave copy
    if (file) {
        file << post.posted_by() << endl;
        file << time << endl;
        file << post.msg() << endl;
    }
    file.close();
    mu_.unlock();
}


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

vector<Post> getPosts(string name, int tm) {
    Post post;
    vector<Post> posts;
    string message;
    string uname;
    string time;
    time_t t;
    Timestamp timestamp;

    ifstream file;
    if (tm == 0) { file.open(ma_dir + name + "newposts.txt");}
    else if (tm == 1) { file.open(sl_dir + name + "newposts.txt");}
    while(!file.eof()) {
        getline(file, uname);
        if (uname == "") {
            break;
        }
        getline(file, time);
        getline(file, message);

        post.set_posted_by(stoi(uname));
        post.set_msg(message);
        // post.set_follower_user(follower);

        stringstream ss(time);
        ss >> t;
        timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(t);
        
        post.set_allocated_timestamp(&timestamp);
        posts.push_back(post);
        post.release_timestamp();
    }
    file.close();

    ofstream ofile;
    ofile.open(ma_dir + name + "newposts.txt");   // erase contents of file
    ofile.close();

    ofile.open(sl_dir + name + "newposts.txt");   // erase contents of file
    ofile.close();
    return posts;
}

void syncNewPosts() {
    ofstream file;
    time_t time;
    vector<string> local = populateLocal();
    for (int i = 0; i < local.size(); ++i) {
        vector<Post> posts = getPosts(local[i], 0);
        file.open(sl_dir + local[i] + "newposts.txt");
        for (int j = 0; j < posts.size(); ++j) {
            time = google::protobuf::util::TimeUtil::TimestampToTimeT(posts[j].timestamp());
            file << posts[j].posted_by() << endl;
            file << time << endl;
            file << posts[j].msg() << endl;
        }
        file.close();
    }

}


class SNSFollowSyncImpl final : public SNSFollowSync::Service {

    Status SyncUsers(ServerContext* context, const Users* users, Reply* reply) { //does nothing
        //maintain local list of all users across all clusters
        
        return Status::OK;
    }
    
    Status SyncRelations(ServerContext* context, const Relation* relation, Reply* reply) {
        
        //maintain follower relations among local users and followers file system
        //vet if relation already exists, if not, edit file

        unique_lock<mutex> lock(mu_);
        vector<string> followers = populateFollowers(to_string(relation->followee()));
        for (int i = 0; i < followers.size(); ++i) {
            if (followers[i] == to_string(relation->follower())) {
                return Status::OK;
            }
        }

        ofstream file;
        file.open(sl_dir + to_string(relation->followee()) + "followers.txt", ios::app);
        file << to_string(relation->follower()) << endl;
        file.close();

        file.open(ma_dir + to_string(relation->followee()) + "followers.txt", ios::app);
        file << to_string(relation->follower()) << endl;
        file.close();

        Relation rel;              //add to list of relations we are aware of
        rel.CopyFrom(*relation);
        relations.push_back(rel);

        return Status::OK;
    }

    Status SyncTimeline(ServerContext* context, const Post* post, Reply* reply) {
        // write post from a user to timeline of specified local follower in post
        Post post0;
        post0.CopyFrom(*post);
        writeToTimeline(post0);

        return Status::OK;
    }
};


void map_syncs() {  //creates map of users to their respective follower synchronizers
    //recreate map
    for (int i = 0; i < syncs.users_size(); ++i) {
        if (user_syncs.count(syncs.users()[i]) != 0) {
            continue;
        }
        syncron s;
        s.ip = syncs.follow_sync_ip()[i];
        s.port = syncs.port_nums()[i];
        // cout << "user: " << syncs.users()[i] << " port: " << s.port << endl;
        user_syncs.insert({syncs.users()[i], s});
    }

}

void updateUsers() {   //updates the local copy of list of all users based on coordinator's list
    // unique_lock<mutex> lock(mu_);
    grpc::ClientContext context;
    ClusterId cl;
    snsCoordinator::Users users;
    cl.set_cluster(stoi(id));
    Status status = coordstub_->GetAllUsers(&context, cl, &users);

    ofstream userstream;
    string username;
    
    all_users.clear();
    // get all current users and update local vector
    userstream.open(sl_dir + "allusers.txt");         //slave copy
    for (int i = 0; i < users.users_size(); ++i) {
        userstream << to_string(users.users()[i]) << endl;
        all_users.push_back(users.users()[i]);
    }
    userstream.close();
    
    userstream.open(ma_dir + "allusers.txt");         //master copy
    for (int i = 0; i < users.users_size(); ++i) {
        userstream << to_string(users.users()[i]) << endl;
    }
    userstream.close();

    grpc::ClientContext context0;
    snsCoordinator::Users users0;
    FollowSyncs fs;
    Status status0;

    if (all_users.size() != 0) {
        for (int i = 0; i < all_users.size(); ++i) {
            users0.add_users(all_users[i]);
        }

        status = coordstub_->GetFollowSyncsForUsers(&context0, users0, &fs);
        syncs.CopyFrom(fs);

        map_syncs();
    }
}

void syncRelationsLocal(Relation relation) {  //local version of rpc call, for local follows that don't require external call
    // unique_lock<mutex> lock(mu_);
    //vet if relation already exists, if not, edit file
    bool found = false;
    vector<string> followers = populateFollowers(to_string(relation.followee()));
    for (int i = 0; i < followers.size(); ++i) {
        if (followers[i] == to_string(relation.follower())) {
            // cout << "ignoring local sync" << endl;
            return;
        }
    }
    vector<string> local = populateLocal();
    for (int i = 0; i < local.size(); ++i) {
        if (local[i] == to_string(relation.followee())) {
            // cout << "ignoring cause not local" << endl;
            found = true;
        }
    }

    if (!found) { return;}
    // cout << "syncing locally" << endl;

    ofstream file;
    file.open(sl_dir + to_string(relation.followee()) + "followers.txt", ios::app);
    file << to_string(relation.follower()) << endl;
    file.close();

    file.open(ma_dir + to_string(relation.followee()) + "followers.txt", ios::app);
    file << to_string(relation.follower()) << endl;
    file.close();

    Relation rel;              //add to list of relations we are aware of
    rel.CopyFrom(relation);
    relations.push_back(rel);
}

void syncTimelines() {
    grpc::ClientContext context;
    Reply reply;
    Status status;
    //for each local user, see if there have been changes/updates, if there have, 
    // write them to the corresponding relation
    vector<string> local = populateLocal();
    for (int i = 0; i < local.size(); ++i) {
        vector<string> followers = populateFollowers(local[i]);  //get list of people following the local user
        vector<Post> posts = getPosts(local[i], 1);
        for (int j = 0; j < followers.size(); ++j) {
            for (int n = 0; n < posts.size(); ++n) {
                posts[n].set_follower_user(stoi(followers[j]));
                grpc::ClientContext context;
                Reply reply;
                Status status;
                auto it = user_syncs.find(stoi(followers[j]));
                if (it->second.port != port) { 
                    syncstub_ = SNSFollowSync::NewStub(CreateChannel(it->second.ip + ":" + it->second.port, grpc::InsecureChannelCredentials()));
                    status = syncstub_->SyncTimeline(&context, posts[n], &reply);
                }
                else { writeToTimeline(posts[n]);}
            }
        }
    }

}

void syncRelations() {  // synchronizes set of relations across clusters

    for (int i = 0; i < relations.size(); ++i) {
        grpc::ClientContext context;
        Reply reply;
        Status status;
        auto it = user_syncs.find(relations[i].followee());  //person being followed in every entry
        
        //send syncRelation rpc to corresponding followerSynchronizer for each relation
        if (it->second.port != port) {
            syncstub_ = SNSFollowSync::NewStub(CreateChannel(it->second.ip + ":" + it->second.port, grpc::InsecureChannelCredentials()));
            status = syncstub_->SyncRelations(&context, relations[i], &reply);
        }
        else {
            syncRelationsLocal(relations[i]);
        }
    }
}

void updateRelations() { //updates the relations vector (synchronized set of known relations across clusters)
    // unique_lock<mutex> lock(mu_);
    grpc::ClientContext context;
    snsCoordinator::Users users;
    FollowSyncs fs;
    Status status;

    ifstream file;
    string user;
    vector<string> local;
    file.open(dir + "users.txt");
    while(!file.eof()) {
        getline(file, user);
        local.push_back(user);
    }
    local.pop_back();
    file.close();
    
    relations.clear();
    for (int i = 0; i < local.size(); ++i) {                     //reset all known relations
        vector<string> followers = populateFollowers(local[i]);
        vector<string> following = populateFollowing(local[i]);
        for (int j = 0; j < followers.size(); ++j) {
            Relation rel;
            rel.set_followee(stoi(local[i]));
            rel.set_follower(stoi(followers[j]));
            relations.push_back(rel);
        }
        for (int j = 0; j < following.size(); ++j) {
            Relation rel;
            rel.set_followee(stoi(following[j]));
            rel.set_follower(stoi(local[i]));
            relations.push_back(rel);
        }
    }

    syncRelations();
}

//initializes synchronizer and launches heartbeat thread
//hb thread updates state of all neccessary files every sleep(x) seconds, or immediately with no sleep(). 
void RunSynchronizer(std::string port_no) {
    sl_dir = "slave_" + id + "/";
    ma_dir = "master_" + id + "/";
    dir = "slave_" + id + "/";
    SNSFollowSyncImpl service;
    ServerBuilder builder;

    builder.AddListeningPort(port_no, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    unique_ptr<grpc::Server> sync(builder.BuildAndStart());
    coordstub_ = SNSCoordinator::NewStub(CreateChannel(cip + ":" + cp, grpc::InsecureChannelCredentials()));
    
    grpc::ClientContext coordcontext;
    std::unique_ptr<grpc::ClientReaderWriter<Heartbeat, Heartbeat>> stream(coordstub_->HandleHeartBeats(&coordcontext));
  
    Heartbeat ping;
    ServerType st;
    time_t time;

    st = ServerType::SYNC;

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
    
    thread update([&stream] () {  //thread for heartbeats to coordinator, synchronizes on sleep timer
        // unique_lock<mutex> lock(mu_);
        time_t time;
        Heartbeat ping;
        while(stream->Read(&ping)) {
            auto const now = chrono::system_clock::now();
            time = chrono::system_clock::to_time_t(now);
            Timestamp ts = google::protobuf::util::TimeUtil::TimeTToTimestamp(time);
            ping.set_allocated_timestamp(&ts);
            if (!stream->Write(ping)) { break;}
            ping.release_timestamp();
            // cout << "relations size: " << relations.size() << endl;
            updateUsers(); //update list of all users
            updateRelations(); //update relation set and respective timelines
            syncNewPosts();
            syncTimelines();
            sleep(hb);
        }
    });
    update.detach();

    sync->Wait();

}

int main(int argc, char** argv) {
    
    // 3010 default port no, or 10000
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == string("-cip") && i + 1 < argc) { cip = argv[++i];}
        else if (argv[i] == string("-cp") && i + 1 < argc) { cp = argv[++i];}
        else if (argv[i] == string("-p") && i + 1 < argc) { port = argv[++i];}
        else if (argv[i] == string("-id") && i + 1 < argc) { id = argv[++i];}
        else if (argv[i] == string("-h") && i + 1 < argc) { hb = stoi(argv[++i]);}
        else {cerr << "Invalid Command Line Argument\n";}
    }

    RunSynchronizer(def + port);
    return 0;
}