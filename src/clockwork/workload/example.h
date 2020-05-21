#ifndef _CLOCKWORK_WORKLOAD_EXAMPLE_H_
#define _CLOCKWORK_WORKLOAD_EXAMPLE_H_

#include "clockwork/util.h"
#include "clockwork/workload/workload.h"
#include "clockwork/workload/azure.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace clockwork {
namespace workload {

Engine* fill_memory(clockwork::Client* client) {
	Engine* engine = new Engine();

	unsigned num_copies = 411;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];
	auto models = client->load_remote_models(modelpath, num_copies);

	unsigned i = 0;
	for (; i < 1; i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new PoissonOpenLoop(
			i,				// client id
			models[i],  	// model
			i,      		// rng seed
			1000				// requests/second
		));
	}
	for (; i < 11; i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new PoissonOpenLoop(
			i,				// client id
			models[i],  	// model
			i,      		// rng seed
			10				// requests/second
		));
	}
	for (; i < 411; i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new PoissonOpenLoop(
			i,				// client id
			models[i],  	// model
			i,      		// rng seed
			1				// requests/second
		));
	}

	return engine;
}

Engine* simple(clockwork::Client* client) {
	Engine* engine = new Engine();

	unsigned num_copies = 2;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];
	auto models = client->load_remote_models(modelpath, num_copies);

	for (unsigned i = 0; i < models.size(); i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new ClosedLoop(
			0, 			// client id, just use the same for this
			models[i],	// model
			1			// concurrency
		));
	}

	return engine;
}

Engine* simple_slo_factor(clockwork::Client* client) {
	Engine* engine = new Engine();

	unsigned num_copies = 3;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];
	auto models = client->load_remote_models(modelpath, num_copies);

	for (unsigned i = 0; i < models.size(); i++) {
		engine->AddWorkload(new ClosedLoop(
			0, 			// client id, just use the same for this
			models[i],	// model
			1			// concurrency
		));
	}

	// Adjust model 0 multiplicatively
	engine->AddWorkload(new AdjustSLO(
		10.0, 					// period in seconds
		4.0,					// initial slo_factor
		{models[0]},			// The model or models to apply the SLO adjustment to
		[](float current) { 	// SLO update function
			return current * 1.25; 
		},
		[](float current) { return false; } // Terminate condition
	));

	// Adjust model 1 additively
	engine->AddWorkload(new AdjustSLO(
		10.0, 					// period in seconds
		4.0,					// initial slo_factor
		{models[1]},			// The model or models to apply the SLO adjustment to
		[](float current) { 	// SLO update function
			return current + 1.0; 
		},
		[](float current) { return false; } // Terminate condition
	));

	// Adjust model 2 back and forth
	engine->AddWorkload(new AdjustSLO(
		10.0, 					// period in seconds
		10,						// initial slo_factor
		{models[2]},			// The model or models to apply the SLO adjustment to
		[](float current) { 	// SLO update function
			return current = 10 ? 1 : 10;
		},
		[](float current) { return false; } // Terminate condition
	));

	return engine;	
}

Engine* simple_parametric(clockwork::Client* client, unsigned num_copies,
	unsigned num_clients, unsigned concurrency, unsigned num_requests) {
	Engine* engine = new Engine();

	unsigned num_models_per_client = ceil(num_copies / (float)num_clients);
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];
	auto models = client->load_remote_models(modelpath, num_copies);

	for (unsigned i = 0; i < num_clients; i++) {

		// note that std::vector initialization ignores the last element
		auto it1 = models.begin() + (i * num_models_per_client);
		auto it2 = models.begin() + ((i + 1) * num_models_per_client);
		std::vector<clockwork::Model*> models_subset(it1, std::min(it2, models.end()));

		std::cout << "Adding ClosedLoop client " << i << " with models:";
		for (auto const &model : models_subset) {
			model->disable_inputs();
			std::cout << " " << model->id();
		}
		std::cout << std::endl;

		engine->AddWorkload(new ClosedLoop(
			i, 				// client id
			models_subset,	// subset of models
			concurrency,	// concurrency
			num_requests,	// max num requests
			0				// jitter
		));

		if (it2 >= models.end()) { break; }
	}

	return engine;
}

Engine* poisson_open_loop(clockwork::Client* client, unsigned num_models,
	double rate) {
	Engine* engine = new Engine();

	std::string model_name = "resnet50_v2";
	std::string modelpath = util::get_clockwork_modelzoo()[model_name];

	std::cout << "Loading " << num_models << " " << model_name
			  << " models" << std::endl;
	std::cout << "Cumulative request rate across models: " << rate
			  << " requests/seconds" << std::endl;
	auto models = client->load_remote_models(modelpath, num_models);

	std::cout << "Adding a PoissonOpenLoop Workload (" << (rate/num_models)
			  << " requests/second) for each model" << std::endl;
	for (unsigned i = 0; i < models.size(); i++) {
		engine->AddWorkload(new PoissonOpenLoop(
			i,				// client id
			models[i],  	// model
			i,      		// rng seed
			rate/num_models	// requests/second
		));
	}

	return engine;
}

Engine* example(clockwork::Client* client) {
	Engine* engine = new Engine();

	unsigned client_id = 0;
	for (auto &p : util::get_clockwork_modelzoo()) {
		std::cout << "Loading " << p.first << std::endl;
		auto model = client->load_remote_model(p.second);

		Workload* fixed_rate = new FixedRate(
			client_id,		// client id
			model,  		// model
			0,      		// rng seed
			5				// requests/second
		);
		Workload* open = new PoissonOpenLoop(
			client_id,		// client id
			model,			// model
			1,				// rng seed
			10				// request/second
		);
		Workload* burstyopen = new BurstyPoissonOpenLoop(
			client_id, 		// client id
			model, 			// model
			1,				// rng seed
			10,				// requests/second
			10,				// burst duration
			20				// idle duration
		);
		Workload* closed = new ClosedLoop(
			client_id, 		// client id
			model,			// model
			1				// concurrency
		);
		Workload* burstyclosed = new BurstyPoissonClosedLoop(
			client_id, 		// client id
			model, 			// model
			1,				// concurrency
			0,				// rng seed
			10, 			// burst duration
			20				// idle duration
		);

		engine->AddWorkload(fixed_rate);
		engine->AddWorkload(open);
		engine->AddWorkload(burstyopen);
		engine->AddWorkload(closed);
		engine->AddWorkload(burstyclosed);

		client_id++;
	}

	return engine;
}

Engine* spam(clockwork::Client* client, std::string model_name = "resnet50_v2") {
	Engine* engine = new Engine();

	unsigned num_copies = 100;
	std::string modelpath = util::get_clockwork_modelzoo()[model_name];
	auto models = client->load_remote_models(modelpath, num_copies);

	for (unsigned i = 0; i < models.size(); i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new ClosedLoop(
			0, 			// client id, just use the same for this
			models[i],	// model
			100			// concurrency
		));
	}

	return engine;
}

Engine* spam2(clockwork::Client* client) {
	Engine* engine = new Engine();

	unsigned num_copies = 100;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];
	auto models = client->load_remote_models(modelpath, num_copies);

	for (unsigned i = 0; i < models.size(); i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new PoissonOpenLoop(
			i,				// client id
			models[i],  	// model
			i,      		// rng seed
			1000			// requests/second
		));
	}

	return engine;
}

Engine* single_spam(clockwork::Client* client) {
	Engine* engine = new Engine();

	unsigned num_copies = 1;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];
	auto models = client->load_remote_models(modelpath, num_copies);

	for (unsigned i = 0; i < models.size(); i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new ClosedLoop(
			0, 			// client id, just use the same for this
			models[i],	// model
			1000			// concurrency
		));
	}

	return engine;
}

Engine* azure(clockwork::Client* client) {
	Engine* engine = new Engine();

	auto trace_data = azure::load_trace();

	unsigned trace_id = 1;
	std::string model = util::get_clockwork_model("resnet50_v2");
	unsigned num_copies = 200;

	auto models = client->load_remote_models(model, num_copies);

	for (unsigned i = 0; i < trace_data.size(); i++) {
		auto model = models[i % num_copies];
		auto workload = trace_data[i];

		Workload* replay = new PoissonTraceReplay(
			0,				// client id, just give them all the same ID for this example
			model,			// model
			i,				// rng seed
			workload,		// trace data
			1.0,			// scale factor; default 1
			60.0,			// interval duration; default 60
			0				// interval to begin at; default 0; set to -1 for random
		);

		engine->AddWorkload(replay);
	}

	return engine;
}

Engine* azure_single(clockwork::Client* client) {
	Engine* engine = new Engine();

	auto trace_data = azure::load_trace();

	unsigned trace_id = 1;
	std::string model = util::get_clockwork_model("resnet50_v2");
	unsigned num_copies = 1;

	auto models = client->load_remote_models(model, num_copies);

	for (unsigned i = 0; i < num_copies; i++) {
		auto model = models[i % num_copies];
		auto workload = trace_data[i];

		Workload* replay = new PoissonTraceReplay(
			0,				// client id, just give them all the same ID for this example
			model,			// model
			i,				// rng seed
			workload,		// trace data
			1.0,			// scale factor; default 1
			1.0,			// interval duration; default 60
			0				// interval to begin at; default 0; set to -1 for random
		);

		engine->AddWorkload(replay);
	}

	return engine;
}

Engine* azure_small(clockwork::Client* client) {
	Engine* engine = new Engine();

	auto trace_data = azure::load_trace(1);

	std::vector<Model*> models;
	for (auto &p : util::get_clockwork_modelzoo()) {
		std::cout << "Loading " << p.first << std::endl;
		for (auto &model : client->load_remote_models(p.second, 3)) {
			models.push_back(model);
		}
	}

	for (unsigned i = 0; i < trace_data.size(); i++) {
		auto model = models[i % models.size()];
		model->disable_inputs();
		auto workload = trace_data[i];

		Workload* replay = new PoissonTraceReplay(
			i,				// client id, just give them all the same ID for this example
			model,			// model
			i,				// rng seed
			workload,		// trace data
			1.0,			// scale factor; default 1
			60.0,			// interval duration; default 60
			0				// interval to begin at; default 0; set to -1 for random
		);

		engine->AddWorkload(replay);
	}

	return engine;
}

Engine* azure_fast(clockwork::Client* client, unsigned trace_id = 1) {
	Engine* engine = new Engine();

	auto trace_data = azure::load_trace(trace_id);

	std::vector<Model*> models;
	for (auto &p : util::get_clockwork_modelzoo()) {
		std::cout << "Loading " << p.first << std::endl;
		for (auto &model : client->load_remote_models(p.second, 3)) {
			models.push_back(model);
		}
	}


	for (unsigned i = 0; i < trace_data.size(); i++) {
		auto model = models[i % models.size()];
		model->disable_inputs();
		auto workload = trace_data[i];

		Workload* replay = new PoissonTraceReplay(
			i,				// client id, just give them all the same ID for this example
			model,			// model
			i,				// rng seed
			workload,		// trace data
			1.0,			// scale factor; default 1
			1.0,			// interval duration; default 60
			-1				// interval to begin at; default 0; set to -1 for random
		);

		engine->AddWorkload(replay);
	}

	return engine;
}

Engine* bursty_experiment(clockwork::Client* client, unsigned trace_id = 1) {
	Engine* engine = new Engine();

	unsigned num_models = 3600;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];

	unsigned intervals = 30;
	double interval_duration_seconds = 20;
	unsigned num_gpus = 8;
	unsigned total_request_rate = 1000 * num_gpus;

	std::vector<std::vector<unsigned>> interval_rates;
	for (unsigned i = 0; i < num_models; i++) {
		interval_rates.push_back(std::vector<unsigned>(intervals, 0));
	}

	for (unsigned i = 0; i < intervals; i++) {
		unsigned models_this_interval = std::min((unsigned) round(std::pow(2, i/2.0)), num_models);
		std::cout << "Interval " << i << " " << models_this_interval << " models" << std::endl;

		for (int j = 0; j < 60 * total_request_rate; j++) {
			unsigned model = j % models_this_interval;
			interval_rates[model][i]++;
		}
	}

	for (unsigned i = 0; i < 5; i++) {
		std::cout << "Model " << i;
		for (unsigned j = 0; j < intervals; j++) {
			std::cout << " " << interval_rates[i][j];
		}
		std::cout << std::endl;
	}
	std::cout << "..." << std::endl;
	for (unsigned i = num_models - 6; i < num_models; i++) {
		std::cout << "Model " << i;
		for (unsigned j = 0; j < intervals; j++) {
			std::cout << " " << interval_rates[i][j];
		}
		std::cout << std::endl;
	}

	std::vector<Model*> models;
	while (models.size() < num_models) {
		unsigned to_load = std::min((int) (num_models - models.size()), 100);
		std::cout << "Loading " << models.size() << ":" << models.size()+to_load << std::endl;
		for (auto &model : client->load_remote_models(modelpath, to_load)) {
			models.push_back(model);
		}
	}

	for (unsigned i = 0; i < models.size(); i++) {
		auto model = models[i];
		model->disable_inputs();
		auto workload = interval_rates[i];

		Workload* replay = new PoissonTraceReplay(
			i,	// client id
			model,	// model
			i, 		// rng seed
			workload, 	// synthetic trace data
			1, 		// scale factor
			interval_duration_seconds,		// interval duration
			0		// start interval
		);

		engine->AddWorkload(replay);
	}

	return engine;
}


	// PoissonTraceReplay(int id, clockwork::Model* model, int rng_seed,
	// 	std::vector<unsigned> &interval_rates, // request rate for each interval, specified as requests per minute
	// 	double scale_factor = 1.0,
	// 	double interval_duration_seconds = 60.0,
	// 	int start_at = 0 // which interval to start at.  if set to -1, will randomise
	// )

Engine* bursty_experiment1(clockwork::Client* client, unsigned trace_id = 1) {
	Engine* engine = new Engine();

	unsigned num_copies = 3600;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];

	std::vector<Model*> models;
	for (int i = 0; i < num_copies; i+=100) {
		std::cout << "Loading " << i << ":" << i+100 << std::endl;
		for (auto &model : client->load_remote_models(modelpath, 100)) {
			models.push_back(model);
		}
	}

	unsigned num_iterations = 10;
	uint64_t iteration_length = 60000000000UL; // 60 seconds

	unsigned increase_per_iteration = ((num_copies-1) / num_iterations) + 1;
	unsigned model_id = 0;
	for (unsigned i = 0; i < num_iterations; i++) {
		unsigned iteration_max = increase_per_iteration * (i+1);
		while (model_id < iteration_max && model_id < models.size()) {
			models[model_id]->disable_inputs();
			engine->AddWorkload(new PoissonOpenLoop(
				model_id,				// client id
				models[model_id],  	// model
				model_id,      		// rng seed
				1			// requests/second
			), i * iteration_length);
			model_id++;
		}
	}

	return engine;
}

Engine* bursty_experiment_simple(clockwork::Client* client, unsigned trace_id = 1) {
	Engine* engine = new Engine();

	unsigned num_copies = 3600;
	std::string modelpath = util::get_clockwork_modelzoo()["resnet50_v2"];

	std::vector<Model*> models;
	for (int i = 0; i < num_copies; i+=100) {
		std::cout << "Loading " << i << ":" << i+100 << std::endl;
		for (auto &model : client->load_remote_models(modelpath, 100)) {
			models.push_back(model);
		}
	}

	for (unsigned i = 0; i < models.size(); i++) {
		models[i]->disable_inputs();
		engine->AddWorkload(new PoissonOpenLoop(
			i,				// client id
			models[i],  	// model
			i,      		// rng seed
			0.1			// requests/second
		));
	}

	return engine;
}

Engine* azure_half(clockwork::Client* client, unsigned trace_id = 1) {
	Engine* engine = new Engine();

	auto trace_data = azure::load_trace(trace_id);

	std::vector<Model*> models;
	for (auto &p : util::get_clockwork_modelzoo()) {
		std::cout << "Loading " << p.first << std::endl;
		for (auto &model : client->load_remote_models(p.second, 50)) {
			models.push_back(model);
		}
	}


	for (unsigned i = 0; i < trace_data.size(); i++) {
	// for (unsigned i = 0; i < models.size(); i++) {
		auto model = models[i % models.size()];
		model->disable_inputs();
		auto workload = trace_data[i];

		Workload* replay = new PoissonTraceReplay(
			i,				// client id, just give them all the same ID for this example
			model,			// model
			i,				// rng seed
			workload,		// trace data
			1,			// scale factor; default 1
			60,			// interval duration; default 60
			0				// interval to begin at; default 0; set to -1 for random
		);

		engine->AddWorkload(replay);
	}

	return engine;
}

}
}

#endif 

