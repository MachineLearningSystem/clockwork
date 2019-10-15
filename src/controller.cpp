#include "clockwork/network/worker_net.h"
#include "clockwork/network/client_net.h"
#include "clockwork/api/client_api.h"
#include "clockwork/api/worker_api.h"
#include <cstdlib>
#include <unistd.h>
#include <libgen.h>
#include "clockwork/test/util.h"
#include <catch2/catch.hpp>
#include <nvml.h>
#include <iostream>
#include "clockwork/util.h"

using namespace clockwork;

std::string get_clockwork_dir() {
    int bufsize = 1024;
    char buf[bufsize];
    int len = readlink("/proc/self/exe", buf, bufsize);
	return dirname(dirname(buf));
}

std::string get_example_model(std::string name = "resnet18_tesla-m40_batchsize1") {
    return get_clockwork_dir() + "/resources/" + name + "/model";
}

class WorkerResultsPrinter : public workerapi::Controller {
public:
	std::atomic_int results_count = 0;

	// workerapi::Controller::sendResult
	virtual void sendResult(std::shared_ptr<workerapi::Result> result) {
		std::cout << "Received result " << result->str() << std::endl;
		results_count++;
	}

};

class ClientRequestsPrinter : public clientapi::ClientAPI {
public:

	virtual void uploadModel(clientapi::UploadModelRequest &request, std::function<void(clientapi::UploadModelResponse&)> callback) {
		std::cout << "Controller uploadModel" << std::endl;
		clientapi::UploadModelResponse rsp;
		callback(rsp);
	}

	virtual void infer(clientapi::InferenceRequest &request, std::function<void(clientapi::InferenceResponse&)> callback) {
		std::cout << "Controller uploadModel" << std::endl;
		clientapi::InferenceResponse rsp;
		callback(rsp);
	}

	/** This is a 'backdoor' API function for ease of experimentation */
	virtual void evict(clientapi::EvictRequest &request, std::function<void(clientapi::EvictResponse&)> callback) {
		std::cout << "Controller uploadModel" << std::endl;
		clientapi::EvictResponse rsp;
		callback(rsp);
	}

	/** This is a 'backdoor' API function for ease of experimentation */
	virtual void loadRemoteModel(clientapi::LoadModelFromRemoteDiskRequest &request, std::function<void(clientapi::LoadModelFromRemoteDiskResponse&)> callback) {
		std::cout << "Controller uploadModel" << std::endl;
		clientapi::LoadModelFromRemoteDiskResponse rsp;
		callback(rsp);
	}
};

int main(int argc, char *argv[]) {
	std::cout << "Starting Clockwork Controller" << std::endl;

	int client_requests_listen_port = 12346;

	std::string worker_connect_hostname = "127.0.0.1";
	std::string worker_connect_port = "12345";

	// Create the client API to listen and receive requests from clients
	ClientRequestsPrinter* client_api_handler = new ClientRequestsPrinter();

	// Create the worker API to receive results from workers
	WorkerResultsPrinter* worker_api_handler = new WorkerResultsPrinter();

	// Create a server to receive requests from clients
	auto client_facing_server = new network::ControllerServer(client_api_handler, client_requests_listen_port);

	// Create the client that handles all worker connections
	auto worker_connections = new network::WorkerClient();

	// Connect to one worker
	auto worker1 = worker_connections->connect(worker_connect_hostname, worker_connect_port, worker_api_handler);

	worker_connections->join();

	// auto load_model = std::make_shared<workerapi::LoadModelFromDisk>();
	// load_model->id = 0;
	// load_model->model_id = 0;
	// load_model->model_path = get_example_model();
	// load_model->earliest = 0;
	// load_model->latest = util::now() + 10000000000L;

	// std::vector<std::shared_ptr<workerapi::Action>> actions;
	// actions = {load_model};

	// worker->sendActions(actions);

	// while (controller->results_count.load() == 0);

	// worker->close();
	// client->shutdown(true);

	std::cout << "Clockwork Worker Exiting" << std::endl;
}