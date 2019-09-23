#include "clockwork-convert/tvm_abstract_model.h"

#include <vector>
#include <algorithm>
#include <string>
#include <cstring>
#include "clockwork-convert/tvm_model.h"
#include <dmlc/logging.h>

#include <dlpack/dlpack.h>

namespace clockwork_model {


const size_t Tensor::Size() {
	size_t size = 1;
	for (unsigned i = 0; i < shape.size(); i++) {
		size *= static_cast<size_t>(shape[i]);
	}
	size_t bits = dltype.bits * dltype.lanes;
	size_t bytes = ((bits + 7U) / 8U) * size;
	return bytes;
}

const size_t StorageLocation::Size() {
	size_t maxSize = 0;
	for (unsigned i = 0; i < used_by.size(); i++) {
		size_t tensorSize = used_by[i]->Size();
		if (tensorSize > maxSize) {
			maxSize = tensorSize;
		}
	}
	return maxSize;
}

Model Model::fromTVM(tvm_model::Model &model, tvm_model::Params &params, tvm_model::Allocs &allocs) {
	Model out;

	std::unordered_map<int, StorageLocation*> storage_locations;
	for (const int &storage_id : model.attrs_.storage_id) {
		if (storage_locations.find(storage_id) == storage_locations.end()) {
			StorageLocation* storage = new StorageLocation();
			storage->id = storage_id;
			storage_locations[storage_id] = storage;
			out.storage_locations.push_back(storage);
		}
	}

	std::vector<Tensor*> tensors;
	for (unsigned i = 0; i < model.attrs_.storage_id.size(); i++) {
		Tensor* tensor = new Tensor();
		tensor->id = i;
		tensor->dltype = tvm::runtime::String2TVMType(model.attrs_.dltype[i]);
		tensor->shape = model.attrs_.shape[i];
		tensor->storage = storage_locations[model.attrs_.storage_id[i]];
		tensors.push_back(tensor);
		tensor->storage->used_by.push_back(tensor);
	}

	for (auto &p : params.data) {
		LayerWeights* weights = new LayerWeights();
		weights->name = p.first;
		weights->data = p.second->dataptr();
		weights->size = p.second->Size();
		weights->tensor = nullptr;
		out.weights[p.first] = weights;
	}

	for (unsigned i = 0; i < model.nodes_.size(); i++) {
		tvm_model::Node &node = model.nodes_[i];

		if (node.op_type == "null") {
			int input_index = model.node_row_ptr_[i];
			int input_offset = 0;
			Tensor* tensor = tensors[input_index + input_offset];

			if (out.weights.find(node.name) == out.weights.end()) {
				// If it doesn't have params specified, then it's an input node
				Input* input = new Input();
				input->name = node.name;
				input->tensor = tensor;
				out.inputs[node.name] = input;
			} else {
				// It's a weights node
				out.weights[node.name]->tensor = tensor;
			}
			continue;
		}

		CHECK(node.op_type == "tvm_op") << "Unexpected op type " << node.op_type << " for node " << node.name;

		Operation* op = new Operation();
		op->id = i;
		op->op_name = node.name;
		op->func_name = node.param.func_name;

		CHECK(node.param.flatten_data == 0) << "flatten_data was " << node.param.flatten_data << " but only 0 is currently supported for node " << node.name;

		for (unsigned j = 0; j < node.inputs.size(); j++) {
			int input_node_id = node.inputs[j].node_id;
			int input_index = model.node_row_ptr_[input_node_id];
			int input_offset = node.inputs[j].index;

			CHECK(node.inputs[j].version == 0) << "Encountered version=" << node.inputs[j].version << " for node " << node.name;

			Tensor* input = tensors[input_index + input_offset];
			op->inputs.push_back(input);
		}

		op->allocs = allocs.ops[i].allocs;

		for (unsigned j = 0; j < node.param.num_outputs; j++) {
			int input_index = model.node_row_ptr_[i];
			int input_offset = j;
			Tensor* output = tensors[input_index + input_offset];
			op->outputs.push_back(output);
		}

		out.operations.push_back(op);
	}

	for (unsigned i = 0; i < model.outputs_.size(); i++) {
		int output_node_id = model.outputs_[i].node_id;
		int output_index = model.node_row_ptr_[output_node_id];
		int output_offset = model.outputs_[i].index;

		CHECK(model.outputs_[i].version == 0) << "Encountered version=" << model.outputs_[i].version << " for output " << i;

		Output* output = new Output();
		output->output_ix = i;
		output->tensor = tensors[output_index +output_offset];
		out.outputs.push_back(output);
	}

	return out;
}

size_t Page::Size() {
	size_t size = 0;
	for (StorageLocation* &location : used_by) {
		size += location->Size();
	}
	return size;
}

struct greater_than_storage_location {
	inline bool operator() (StorageLocation* &b1, StorageLocation* &b2) {
		return b1->Size() > b2->Size();
	}
};

struct greater_than_page_size {
	inline bool operator() (Page* &p1, Page* &p2) {
		return p1->Size() > p2->Size();
	}
};

std::vector<Page*> pack(std::vector<StorageLocation*> locations, size_t page_size) {
	// Sort storage locastions in descending order of size
	std::sort(locations.begin(), locations.end(), greater_than_storage_location());

	// Pack each item into page that minimizes remaining space
	std::vector<Page*> pages;
	for (StorageLocation* &location : locations) {
		Page* dst = nullptr;
		for (Page* &page : pages) {
			if (page->Size() + location->Size() <= page_size) {
				dst = page;
				break;
			}
		}

		if (dst == nullptr) {
			dst = new Page();
			pages.push_back(dst);
		}
		dst->used_by.push_back(location);
		std::sort(pages.begin(), pages.end(), greater_than_page_size());
	}

	return pages;
}

PageMappedStorage PageMappedStorage::calculate(Model model, size_t page_size) {
	std::vector<StorageLocation*> weights_storage;
	std::vector<StorageLocation*> workspace_storage;

	for (auto &p : model.weights) {
		if (p.second == nullptr) {
			std::cout << "p.second is nullptr" << std::endl;
		}
		if (p.second->tensor == nullptr) {
			std::cout << " tensor is nullptr" << std::endl;
		}
		if (p.second->tensor->storage == nullptr) {
			std::cout << "storage is nullptr" << std::endl;
		}
		weights_storage.push_back(p.second->tensor->storage);
	}

	for (auto &l : model.storage_locations) {
		if (std::find(weights_storage.begin(), weights_storage.end(), l) == weights_storage.end()) {
			workspace_storage.push_back(l);
		}
	}

	PageMappedStorage mapped;
	mapped.weights = pack(weights_storage, page_size);
	mapped.workspace = pack(workspace_storage, page_size);

	int maxWorkspacePages = 0;
	for (Operation* &op : model.operations) {
		int total_pages = 0;
		int offset_in_page = 0;
		for (size_t &alloc : op->allocs) {
			if (offset_in_page + alloc > page_size) {
				total_pages++;
				offset_in_page = 0;
			}
			offset_in_page += alloc;
		}
		if (offset_in_page > 0) {
			total_pages++;
		}
		if (total_pages > maxWorkspacePages) {
			maxWorkspacePages = total_pages;
		}
	}

	mapped.allocs = maxWorkspacePages;

	return mapped;
}

void printTensorDef(clockwork::model::PageMappedDLTensorDef def, std::string prefix) {
	std::cout << prefix << def.base_offset << " = [" << def.page << " " << def.page_offset << "] + " << def.size << " shape=[ ";
	for (unsigned i = 0; i < def.shape.size(); i++) {
		std::cout << def.shape[i] << " ";
	}
	std::cout << " ]" << std::endl;
}

void printWorkspaceAlloc(clockwork::model::PageMappedWorkspaceAllocDef def, std::string prefix) {
	std::cout << prefix << "[" << def.page << " " << def.page_offset << "] + " << def.size << std::endl;
}

void printOp(unsigned i, clockwork::model::PageMappedModelDef model, clockwork::model::PageMappedOpDef op, std::string prefix) {
	std::cout << prefix << "Op " << i << " function " << op.so_function;
	std::cout.flush();
	std::cout << " (" << model.so_functions[op.so_function] << "):" << std::endl;
	for (unsigned i = 0; i < op.inputs.size(); i++) {
		printTensorDef(op.inputs[i], prefix+"   ");
	}
	if (op.workspace_allocs.size() > 0) {
		std::cout << prefix << "   " << "Workspace:" << std::endl;
		for (unsigned i = 0; i < op.workspace_allocs.size(); i++) {
			printWorkspaceAlloc(op.workspace_allocs[i], prefix+"    ");
		}
	}
}

void printPageDef(clockwork::model::PageDef def, std::string prefix) {
	std::cout << prefix << "[" << def.base_offset << " +" << def.size << "]" << std::endl;
}

void printNewModel(clockwork::model::PageMappedModelDef model) {
	std::cout << std::endl << "------------------ NEW MODEL ------------------" << std::endl;
	std::cout << model.paged_required_memory << " required memory in paged-mode" << std::endl;
	std::cout << model.minimum_required_memory << " required memory in non-paged mode (min necessary)" << std::endl;
	std::cout << model.weights_memory << " total weights memory" << std::endl;
	std::cout << (model.configured_page_size * model.weights_pages.size()) << " total weights paged on " << model.weights_pages.size() << " pages" << std::endl;
	std::cout << model.total_pages << " pages of size " << model.configured_page_size << " needed" << std::endl;
	std::cout << model.so_functions.size() << " SO functions and " << model.cuda_functions.size() << " CUDA functions" << std::endl;
	std::cout << model.ops.size() << " ops:" << std::endl;
	for (unsigned i = 0; i < model.ops.size(); i++) {
		printOp(i, model, model.ops[i], "  ");
	}
	std::cout << "Inputs:" << std::endl;
	for (unsigned i = 0; i < model.inputs.size(); i++) {
		printTensorDef(model.inputs[i], "   ");
	}
	std::cout << "Outputs:" << std::endl;
	for (unsigned i = 0; i < model.outputs.size(); i++) {
		printTensorDef(model.outputs[i], "   ");
	}
	std::cout << "Weights pages:" << std::endl;
	for (unsigned i = 0; i < model.weights_pages.size(); i++) {
		printPageDef(model.weights_pages[i], "   ");
	}

}

void makeModelDef(Model &model, int page_size, clockwork::model::PageMappedModelDef &output, char* &weights, int &weightsSize) {
	PageMappedStorage mapped = PageMappedStorage::calculate(model, page_size);

	output.minimum_required_memory = 0;
	for (StorageLocation* &location : model.storage_locations) {
		output.minimum_required_memory += location->Size();
	}

	output.weights_memory = 0;
	for (auto &p : model.weights) {
		output.weights_memory += p.second->tensor->Size();
	}

	output.total_pages = mapped.weights.size() + mapped.workspace.size() + mapped.allocs;
	output.configured_page_size = page_size;
	output.paged_required_memory = output.total_pages * output.configured_page_size;


	uint64_t current_offset = 0;
	for (Page* &page : mapped.weights) {
		clockwork::model::PageDef pagedef{current_offset, page->Size()};
		output.weights_pages.push_back(pagedef);
		current_offset += pagedef.size;
	}

	std::unordered_map<std::string, int> so_functions;
	for (Operation* &operation : model.operations) {
		if (so_functions.find(operation->func_name) == so_functions.end()) {
			so_functions[operation->func_name] = output.so_functions.size();
			output.so_functions.push_back(operation->func_name);
		}
	}
	// TODO: cuda functions not currently done, just extracted from the SO

	struct PagePointer {
		unsigned page;
		uint64_t page_offset;
		uint64_t base_offset;
	};
	std::unordered_map<int, PagePointer> storage_location_pointers;

	current_offset = 0;
	for (unsigned i = 0; i < mapped.weights.size() + mapped.workspace.size(); i++) {
		// Construct mapping for storage locations
		Page* page = i < mapped.weights.size() ? mapped.weights[i] : mapped.workspace[i-mapped.weights.size()];
		uint64_t current_page_offset = 0;
		for (StorageLocation* &location : page->used_by) {
			CHECK(storage_location_pointers.find(location->id) == storage_location_pointers.end())
				<< "Storage location " << location->id << " assigned to multiple pages";
			storage_location_pointers[location->id] = PagePointer{i, current_page_offset, current_offset};
			current_page_offset += location->Size();
			current_offset += location->Size();
		}
	}

	for (Operation* &operation : model.operations) {
		clockwork::model::PageMappedOpDef opdef;
		opdef.so_function = so_functions[operation->func_name];

		// Both inputs and outputs get passed as arguments to nodes
		for (unsigned i = 0; i < operation->inputs.size() + operation->outputs.size(); i++) {
			Tensor* tensor = i < operation->inputs.size() ? operation->inputs[i] : operation->outputs[i-operation->inputs.size()];
			PagePointer pageptr = storage_location_pointers[tensor->storage->id];

			clockwork::model::PageMappedDLTensorDef tensordef;
			tensordef.base_offset = pageptr.base_offset;
			tensordef.page = pageptr.page;
			tensordef.page_offset = pageptr.page_offset;
			tensordef.size = tensor->Size();
			tensordef.shape = tensor->shape;

			opdef.inputs.push_back(tensordef);
		}

		int current_workspace_page = mapped.weights.size() + mapped.workspace.size();
		int current_workspace_offset = 0;
		for (size_t alloc : operation->allocs) {
			if (current_workspace_offset + alloc > page_size) {
				current_workspace_page++;
				current_workspace_offset = 0;
			}

			clockwork::model::PageMappedWorkspaceAllocDef allocdef;
			allocdef.page = current_workspace_page;
			allocdef.page_offset = current_workspace_offset;
			allocdef.size = alloc;

			current_workspace_offset += alloc;

			opdef.workspace_allocs.push_back(allocdef);
		}

		output.ops.push_back(opdef);
	}

	for (auto &p : model.inputs) {
		Tensor* tensor = p.second->tensor;
		PagePointer pageptr = storage_location_pointers[tensor->storage->id];

		clockwork::model::PageMappedDLTensorDef tensordef;
		tensordef.base_offset = pageptr.base_offset;
		tensordef.page = pageptr.page;
		tensordef.page_offset = pageptr.page_offset;
		tensordef.size = tensor->Size();
		tensordef.shape = tensor->shape;

		output.inputs.push_back(tensordef);
	}

	for (Output* &o : model.outputs) {
		Tensor* tensor = o->tensor;
		PagePointer pageptr = storage_location_pointers[tensor->storage->id];

		clockwork::model::PageMappedDLTensorDef tensordef;
		tensordef.base_offset = pageptr.base_offset;
		tensordef.page = pageptr.page;
		tensordef.page_offset = pageptr.page_offset;
		tensordef.size = tensor->Size();
		tensordef.shape = tensor->shape;

		output.outputs.push_back(tensordef);
	}
	
	printNewModel(output);

	// Now copy the weights
	weightsSize = output.weights_memory;
	weights = static_cast<char*>(malloc(weightsSize));
	for (auto &p : model.weights) {
		void* data = p.second->data;
		size_t size = p.second->size;
		uint64_t offset = storage_location_pointers[p.second->tensor->storage->id].base_offset;
		CHECK(size == p.second->tensor->Size()) << "Mismatched weights sizes " << size << " != " << p.second->tensor->Size();
		std::memcpy(
			weights + offset, // dst
			data, // src
			size // amount
		);
	}
}

}