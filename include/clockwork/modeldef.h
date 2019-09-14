
#ifndef _CLOCKWORK_MODELDEF_H_
#define _CLOCKWORK_MODELDEF_H_

#include <pods/pods.h>
#include <pods/binary.h>
#include <pods/buffers.h>
#include "dmlc/logging.h"

namespace clockwork {
namespace model {

struct DLTensorDef {
	uint64_t offset;
    uint64_t size;
	std::vector<int64_t> shape;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(offset),
        PODS_MDR(size),
        PODS_MDR(shape)
    )
};

struct WorkspaceAllocDef {
    uint64_t offset;
    uint64_t size;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(offset),
        PODS_MDR(size)
    )
};

struct OpDef {
	std::vector<DLTensorDef> inputs;
	unsigned so_function;
	std::vector<unsigned> cuda_functions;
	std::vector<WorkspaceAllocDef> workspace_allocs;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(inputs),
        PODS_MDR(so_function),
        PODS_MDR(cuda_functions),
        PODS_MDR(workspace_allocs)
    )
};

struct ModelDef {
	uint64_t total_memory;
	uint64_t weights_memory;
    uint64_t workspace_memory;
	std::vector<std::string> so_functions;
	std::vector<std::string> cuda_functions;
	std::vector<OpDef> ops;
    std::vector<DLTensorDef> inputs;
    std::vector<DLTensorDef> outputs;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(total_memory),
        PODS_MDR(weights_memory),
        PODS_MDR(so_functions),
        PODS_MDR(cuda_functions),
        PODS_MDR(ops),
        PODS_MDR(inputs),
        PODS_MDR(outputs)
    )

    static void ReadFrom(const std::string &data, ModelDef &def) {
        pods::InputBuffer in(data.data(), data.size());
        pods::BinaryDeserializer<decltype(in)> deserializer(in);
        pods::Error status = deserializer.load(def);
        CHECK(status == pods::Error::NoError) << "Cannot deserialize minmodel";
    }

    // TODO: currently, src/convert.cpp is the only usage of writing model defs; eventually migrate code here
};

struct PageMappedDLTensorDef {
    uint64_t base_offset;
    unsigned page;
    uint64_t page_offset;
    uint64_t size;
    std::vector<int64_t> shape;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(base_offset),
        PODS_MDR(page),
        PODS_MDR(page_offset),
        PODS_MDR(size),
        PODS_MDR(shape)
    )
};

struct PageMappedWorkspaceAllocDef {
    unsigned page;
    uint64_t page_offset;
    uint64_t size;

    PODS_SERIALIZABLE(1,           
        PODS_MDR(page),       
        PODS_MDR(page_offset),
        PODS_MDR(size)
    )
};

struct PageMappedOpDef {
    std::vector<PageMappedDLTensorDef> inputs;
    unsigned so_function;
    std::vector<unsigned> cuda_functions;
    std::vector<PageMappedWorkspaceAllocDef> workspace_allocs;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(inputs),
        PODS_MDR(so_function),
        PODS_MDR(cuda_functions),
        PODS_MDR(workspace_allocs)
    )
};

struct PageDef {
    uint64_t base_offset;
    uint64_t size;

    PODS_SERIALIZABLE(1,         
        PODS_MDR(base_offset),
        PODS_MDR(size)
    )
};

struct PageMappedModelDef {
    uint64_t paged_required_memory;
    uint64_t minimum_required_memory;
    uint64_t weights_memory;

    std::vector<std::string> so_functions;
    std::vector<std::string> cuda_functions;
    std::vector<PageMappedOpDef> ops;
    std::vector<PageMappedDLTensorDef> inputs;
    std::vector<PageMappedDLTensorDef> outputs;

    unsigned total_pages;
    uint64_t configured_page_size;
    std::vector<PageDef> weights_pages;


    PODS_SERIALIZABLE(1,         
        PODS_MDR(paged_required_memory),
        PODS_MDR(minimum_required_memory),
        PODS_MDR(weights_memory),
        PODS_MDR(so_functions),
        PODS_MDR(cuda_functions),
        PODS_MDR(ops),
        PODS_MDR(inputs),
        PODS_MDR(outputs),
        PODS_MDR(total_pages),
        PODS_MDR(configured_page_size),
        PODS_MDR(weights_pages)
    )

    static void ReadFrom(const std::string &data, PageMappedModelDef &def) {
        pods::InputBuffer in(data.data(), data.size());
        pods::BinaryDeserializer<decltype(in)> deserializer(in);
        pods::Error status = deserializer.load(def);
        CHECK(status == pods::Error::NoError) << "Cannot deserialize minmodel";
    }

    // TODO: currently, src/convert.cpp is the only usage of writing model defs; eventually migrate code here
};

}
}

#endif