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
#include "time.h"

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

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :hostname(hname), username(uname), port(p)
            {}
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        // You can have an instance of the client stub
        // as a member variable.
        unique_ptr<csce438::SNSService::Stub> stub_;
};


int main(int argc, char** argv) {

    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "50051";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
        switch(opt) {
            case 'h':
                hostname = optarg;break;
            case 'u':
                username = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
    // create new stub with hostname and port
    this->stub_ = SNSService::NewStub(CreateChannel(this->hostname + ":" + this->port, grpc::InsecureChannelCredentials()));

    IReply ire;
    grpc::ClientContext context;
    Request request;
    request.set_username(this->username);
    Reply reply;

    grpc::Status status = stub_->Login(&context, request, &reply);
    ire.grpc_status = status;
    if (status.ok()) {
        return 1;
    } else {
        return -1;
    }
}

IReply Client::processCommand(std::string& input)
{
	// ------------------------------------------------------------
	// GUIDE 1:
	// In this function, you are supposed to parse the given input
    // command and create your own message so that you call an 
    // appropriate service method. The input command will be one
    // of the followings:
	//
	// FOLLOW <username>
	// UNFOLLOW <username>
	// LIST
    // TIMELINE
	//
	// ------------------------------------------------------------
	
    // ------------------------------------------------------------
	// GUIDE 2:
	// Then, you should create a variable of IReply structure
	// provided by the client.h and initialize it according to
	// the result. Finally you can finish this function by returning
    // the IReply.
	// ------------------------------------------------------------
    
	// ------------------------------------------------------------
    // HINT: How to set the IReply?
    // Suppose you have "Follow" service method for FOLLOW command,
    // IReply can be set as follow:
    // 
    //     // some codes for creating/initializing parameters for
    //     // service method
    //     IReply ire;
    //     grpc::Status status = stub_->Follow(&context, /* some parameters */);
    //     ire.grpc_status = status;
    //     if (status.ok()) {
    //         ire.comm_status = SUCCESS;
    //     } else {
    //         ire.comm_status = FAILURE_NOT_EXISTS;
    //     }
    //      
    //      return ire;
    // 
    // IMPORTANT: 
    // For the command "LIST", you should set both "all_users" and 
    // "following_users" member variable of IReply.
    // ------------------------------------------------------------
    
    IReply ire;
    grpc::ClientContext context;
    Request request;
    request.set_username(this->username);     //always set request user to current client username
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


    if (cmnd == "LIST") {
        grpc::Status status = stub_->List(&context, request, &reply);
        ire.grpc_status = status;
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
    else if (cmnd == "TIMELINE") {
        processTimeline();
    }
    else if (cmnd == "FOLLOW") {
        request.add_arguments(name);
        grpc::Status status = stub_->Follow(&context, request, &reply);
        ire.grpc_status = status;
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
        grpc::Status status = stub_->UnFollow(&context, request, &reply);
        ire.grpc_status = status;
        if (status.ok()) {
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
    
    
    grpc::ClientContext context;
    grpc::Status status;
    Message msg;

    msg.set_username(this->username);   //set username for msg object
    context.AddMetadata("user", msg.username());
    std::unique_ptr<grpc::ClientReaderWriter<Message, Message>> stream(stub_->Timeline(&context));
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
        stream->Write(msg);             //write to stream
        msg.release_timestamp();
    }
}

