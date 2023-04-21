#include <iostream>
#include <signal.h>
#include <string>
#include <ctime>
#include <chrono>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include <google/protobuf/util/time_util.h>
#include "client.h"
#include "sns.grpc.pb.h"
#include "snsCoordinator.grpc.pb.h"
#include "time.h"

#include<glog/logging.h>
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

vector<thread> threads;
mutex mu_;
bool threadRunning = false;

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :hostname(hname), username(uname), coordport(p)
            {}
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
        virtual int reconnect();

    private:
        Server server;
        std::string hostname;
        std::string username;
        std::string servport;
        std::string coordport;
        bool connected = false;
        bool reconnected = false;
        
        // You can have an instance of the client stub
        // as a member variable.
        unique_ptr<snsCoordinator::SNSCoordinator::Stub> coordstub_;
        unique_ptr<csce438::SNSService::Stub> servstub_;
};


void displayReConnectionMessage (const string& host, const string& port) {
    cout << "Reconnecting to " << host << ":" << port << "..." << endl;
}

int main(int argc, char** argv) {

    // 3010 default port no, or 10000
    string def = "0.0.0.0:";
    string port = "10000";
    string cip = "localhost";
    string cp = "9000";
    string id = "0";
    int opt = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i] == string("-cip") && i + 1 < argc) { cip = argv[++i];}
        else if (argv[i] == string("-cp") && i + 1 < argc) { cp = argv[++i];}
        else if (argv[i] == string("-p") && i + 1 < argc) { port = argv[++i];}
        else if (argv[i] == string("-id") && i + 1 < argc) { id = argv[++i];}
        else {cerr << "Invalid Command Line Argument\n";}
    }

    Client myc(cip, id, cp);
    std::string log_file_name = std::string("client-") + id;
    google::InitGoogleLogging(log_file_name.c_str());
    // You MUST invoke "run_client" function to start business logic
    log(INFO, "Logging Initialized. Client starting...");
    myc.run_client();

    return 0;
}

int Client::reconnect() {
    // unique_lock<mutex> lock(mu_);
    IReply ire;
    grpc::ClientContext context;
    User user;
    user.set_user_id(stoi(this->username));
    Server server;

    //contact coordinator first then connect to the server cluster as done here
    grpc::Status status = coordstub_->GetServer(&context, user, &server);
    this->server.set_server_type(server.server_type());
    ire.grpc_status = status;
    if (status.ok()) {

        // create server stub with new port and set client data members
        this->hostname = server.server_ip();
        this->servport= server.port_num();
        this->servstub_ = SNSService::NewStub(CreateChannel(this->hostname + ":" + this->servport, grpc::InsecureChannelCredentials()));
        
        grpc::ClientContext context;
        Request request;
        request.set_username(this->username);
        Reply reply;

        if (this->server.server_type() == 0) {
            log(INFO, "Reconnected to master server");
            cout << endl;
            displayReConnectionMessage(this->hostname, this->servport);
            this->reconnected = true;
            return 1;
        }

        //contact coordinator first then connect to the server cluster as done here
        // status = servstub_->Login(&context, request, &reply);
        // ire.grpc_status = status;

        if (!threadRunning) {
            displayReConnectionMessage(this->hostname, this->servport);
            thread masterReconn([this] () {
                while(!this->reconnected) {
                    reconnect();
                }
                threadRunning = false;
            });
            threadRunning = true;
            masterReconn.detach();
        }
        log(INFO, "Reconnected to slave server");
        return 1;
    } else {
        cout << "failed to reconnect" << endl;
        this->connected = false;
        return -1;
    }
    return 0;
}

int Client::connectTo()
{
    // create new stub with hostname and port
    if (this->connected) { return reconnect();}
    log(INFO, "Connecting to coordinator");
    this->coordstub_ = SNSCoordinator::NewStub(CreateChannel(this->hostname + ":" + this->coordport, grpc::InsecureChannelCredentials()));
    IReply ire;
    grpc::ClientContext context;
    User user;
    user.set_user_id(stoi(this->username));
    Server server;

    //contact coordinator first then connect to the server cluster as done here

    grpc::Status status = coordstub_->GetServer(&context, user, &server);

    ire.grpc_status = status;
    if (status.ok()) {
        // create server stub with new port and set client data members
        log(INFO, "Connecting to master server");
        this->hostname = server.server_ip();
        this->servport = server.port_num();
        this->servstub_ = SNSService::NewStub(CreateChannel(this->hostname + ":" + this->servport, grpc::InsecureChannelCredentials()));
        
        IReply ire;
        grpc::ClientContext context;
        Request request;
        request.set_username(this->username);
        Reply reply;

        //contact coordinator first then connect to the server cluster as done here
        status = servstub_->Login(&context, request, &reply);
        ire.grpc_status = status;
        
        if (!status.ok()) {
            return -1;
        }
        displayReConnectionMessage(this->hostname, this->servport);
        this->connected = true;
        return 1;
    } else {
        return -1;
    }
}

IReply Client::processCommand(std::string& input)
{
    IReply ire;
    grpc::ClientContext context;
    Request request;
    request.set_username(this->username);
    Reply reply;

    stringstream ss(input);
    string word;
    string name;
    string cmnd;
    ss >> word;
    cmnd = word;

    //turn cmnd into uppercase
    locale loc;
    for (size_t i = 0; i < cmnd.size(); i++)
        cmnd[i] = toupper(cmnd[i], loc);

    if (cmnd != "LIST" && cmnd != "TIMELINE") {
        ss >> word;
        name = word;
        if (name == this->username) {    //make sure client is not trying to follow/unfollow itself
            ire.comm_status = FAILURE_ALREADY_EXISTS;
            return ire;
        }
    }

    log(INFO, "Processing "+input);
    if (cmnd == "LIST") {
        grpc::Status status = servstub_->List(&context, request, &reply);
        ire.grpc_status = status;
        if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            connectTo();
            ire.grpc_status = status.OK;
            ire.comm_status = FAILURE_DISCONNECTED;
            return ire;
        }
        if (status.ok()) {
            ire.comm_status = SUCCESS;
            for (auto i : reply.all_users()) {
                ire.all_users.push_back(i);
            }
            for (auto i : reply.following_users()) {
                ire.following_users.push_back(i);
            }
        } 
        else {
            if (ire.grpc_status.error_code() == grpc::StatusCode::ALREADY_EXISTS) {
                ire.grpc_status = Status::OK;
                ire.comm_status = FAILURE_ALREADY_EXISTS;
            }
            else if (ire.grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
                ire.grpc_status = Status::OK;
                ire.comm_status = FAILURE_INVALID_USERNAME;
            }
        }
    }
    else if (cmnd == "TIMELINE") { //if timeline exits then reprompt
        grpc::Status status;
        processTimeline();
        log(INFO, "Timeline returned due to disconnect");
        connectTo();
        ire.grpc_status = status.OK;
        ire.comm_status = FAILURE_DISCONNECTED;
        return ire;
    }
    else if (cmnd == "FOLLOW") {
        request.add_arguments(name);
        grpc::Status status = servstub_->Follow(&context, request, &reply);
        ire.grpc_status = status;
        if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            connectTo();
            ire.grpc_status = status.OK;
            ire.comm_status = FAILURE_DISCONNECTED;
            return ire;
        }
        if (status.ok()) {
            ire.comm_status = SUCCESS;
        }
        else {
            if (ire.grpc_status.error_code() == grpc::StatusCode::ALREADY_EXISTS) {
                ire.grpc_status = Status::OK;
                ire.comm_status = FAILURE_ALREADY_EXISTS;
            }
            else if (ire.grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
                ire.grpc_status = Status::OK;
                ire.comm_status = FAILURE_INVALID_USERNAME;
            }
        }
    }
    else if (cmnd == "UNFOLLOW") {
        request.add_arguments(name);
        grpc::Status status = servstub_->UnFollow(&context, request, &reply);
        ire.grpc_status = status;
        if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            connectTo();
            ire.grpc_status = status.OK;
            ire.comm_status = FAILURE_DISCONNECTED;
            return ire;
        }
        else if (status.ok()) {
            ire.comm_status = SUCCESS;
        }
        else {
            if (ire.grpc_status.error_code() == grpc::StatusCode::NOT_FOUND) {
                ire.grpc_status = Status::OK;
                ire.comm_status = FAILURE_INVALID_USERNAME;
            }
        }
    }
    else {
        ire.comm_status = FAILURE_INVALID;
    }

    return ire;
}

void Client::processTimeline()
{
	// ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions
    // for both getting and displaying messages in timeline mode.
    // You should use them as you did in hw1.
	// ------------------------------------------------------------

    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
	// ------------------------------------------------------------
    
    log(INFO, "Timeline processing");
    grpc::ClientContext context;
    grpc::Status status;
    Message msg;

    msg.set_username(this->username);   //set username for msg object
    context.AddMetadata("user", msg.username());
    std::unique_ptr<grpc::ClientReaderWriter<Message, Message>> stream(servstub_->Timeline(&context));
    cout << "Command completed successfully"<< endl;
    cout << "Now you are in the timeline"<< endl;
    
    thread reader([&stream] () { //launch reading thread
        Message msg;
        time_t time;

        while(stream->Read(&msg)) {
            if (msg.msg() == "") { continue;}
            time = google::protobuf::util::TimeUtil::TimestampToTimeT(msg.timestamp());
            displayPostMessage(msg.username(), msg.msg(), time);
        }
    });
    reader.detach();

    auto const now = chrono::system_clock::now();
    time_t time = chrono::system_clock::to_time_t(now);
    string str;
    while (true) {      //writing "thread"
        str = getPostMessage();
        msg.set_msg(str);  //get msg from user
        auto const now = chrono::system_clock::now();
        time = chrono::system_clock::to_time_t(now);
        Timestamp t = google::protobuf::util::TimeUtil::TimeTToTimestamp(time);
        msg.set_allocated_timestamp(&t);
        if (!stream->Write(msg)) { //write to stream
            cout << "Disconnected..." << endl;
            msg.release_timestamp();
            break;
        }
        msg.release_timestamp();
    }
}

