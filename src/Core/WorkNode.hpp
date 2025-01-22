#pragma once

#include <Core/CommandContext.hpp>
#include <Core/TransientResourceCache.hpp>
#include <iostream>
#include <stack>
#include <functional>
#include <optional>

namespace RoseEngine {

using WorkNodeId = std::string;
using WorkResource = std::variant<
	ConstantParameter,
	BufferParameter,
	ImageParameter,
	AccelerationStructureParameter>;

struct WorkContext {
	CommandContext& cmd;
	std::unordered_map<WorkNodeId, NameMap<WorkResource>> nodeOutputs;

	inline std::optional<WorkResource> GetNodeOutput(const WorkNodeId& node, const std::string& output) const {
		if (auto it = nodeOutputs.find(node); it != nodeOutputs.end()) {
			const auto& outputs = it->second;
			if (auto it2 = outputs.find(output); it2 != outputs.end())
				return it2->second;
		}
		return std::nullopt;
	}
};

class WorkNode {
protected:
	WorkNodeId mId;
	NameMap<std::pair<WorkNodeId, std::string/*output name*/>> mInputs;
	std::vector<std::string> mOutputs;

public:
	inline const WorkNodeId& GetId() const { return mId; }
	inline const auto& GetInputs() const { return mInputs; }
	inline const auto& GetOutputNames() const { return mOutputs; }

	template<one_of<ConstantParameter,
		BufferParameter,
		ImageParameter,
		AccelerationStructureParameter> T>
	inline std::optional<T> GetInput(const WorkContext& ctx, const std::string& input) const {
		const auto&[node, output] = mInputs.at(input);
		if (auto r = ctx.GetNodeOutput(node, output))
			if (T* p = std::get_if<T>(&*r))
				return *p;
		return std::nullopt;
	}
	inline void SetOutput(WorkContext& ctx, const std::string& output, const WorkResource& r) const {
		if (std::ranges::find(mOutputs, output) == mOutputs.end()) {
			std::cout << "Warning: Attempting to write non-existant output " << output << " in node " << mId;
			std::cout << std::endl;
		} else {
			ctx.nodeOutputs[mId][output] = r;
		}
	}

	virtual void execute(WorkContext& ctx) = 0;
};


class WorkGraph {
private:
	std::unordered_map<WorkNodeId, ref<WorkNode>> mNodes;

public:
	inline void execute(WorkNodeId node, CommandContext& context) {
		WorkContext ctx { .cmd = context };

		NameMap<WorkResource> inputs;
		std::unordered_set<WorkNodeId> visited;
		std::stack<WorkNodeId> todo;
		todo.push(node);
		while (!todo.empty()) {
			if (visited.contains(todo.top()))
				continue;
			visited.insert(todo.top());

			auto node_it = mNodes.find(todo.top());
			if (node_it == mNodes.end()) {
				std::cout << "Warning: No node \"" << todo.top() << "\"";
				std::cout << std::endl;
				todo.pop();
				continue;
			}
			WorkNode* n = node_it->second.get();

			bool ready = true;

			// check inputs. if an input node hasn't been executed yet, push it to the stack and wait
			inputs.clear();
			for (const auto& [inputName, connection] : n->GetInputs()) {
				const auto&[srcNodeId, srcOutput] = connection;
				if (!mNodes.contains(srcNodeId)) {
					std::cout << "Warning: No node \"" << srcNodeId << "\"";
					std::cout << " for node/input " << todo.top() << "/" << inputName;
					std::cout << std::endl;
					continue;
				}
				// node exists
				if (auto input_it = ctx.nodeOutputs.find(srcNodeId); input_it != ctx.nodeOutputs.end()) {
					WorkResource r = BufferParameter{};
					if (auto it = input_it->second.find(srcOutput); it != input_it->second.end()) {
						r = it->second;
					} else {
						std::cout << "Warning: Node \"" << srcNodeId << "\" has no output \"" << srcOutput << "\"";
						std::cout << " for node/input " << todo.top() << "/" << inputName;
						std::cout << std::endl;
					}
					inputs[inputName] = r;
				} else {
					// node has not yet been executed
					todo.push(srcNodeId);
					ready = false;
				}
			}

			if (ready) {
				ctx.nodeOutputs[todo.top()] = {};
				n->execute(ctx);
				todo.pop();
			}
		}
	}
};

// ------------------------------------------ Node implementations ------------------------------------------ //

class CreateResourceNode : public WorkNode {
private:
	vk::DeviceSize size;
	vk::BufferUsageFlags usage;
	vk::MemoryPropertyFlags memoryFlags;
	NameMap<TransientResourceCache<WorkResource>> cached;

	inline WorkResource create(const Device& device, const std::string& id) {
		return Buffer::Create(device, size, usage, memoryFlags);
	}

public:
	inline void execute(WorkContext& context) override {
		for (const auto& name : mOutputs) {
			WorkResource r = cached[name].pop_or_create(context.cmd.GetDevice(), [&](){ return create(context.cmd.GetDevice(), name); });
			cached[name].push(r, context.cmd.GetDevice().NextTimelineSignal());
			SetOutput(context, name, r);
		}
	}
};

class ResourceCopyNode : public WorkNode {
public:
	inline void init() {
		mInputs["src"] = {};
		mInputs["dst"] = {};
		mInputs["offset"] = {};
		mInputs["value"] = {};

		mOutputs = { "dst" };
	}

	inline void execute(WorkContext& context) override {
		BufferRange src    = *GetInput<BufferParameter>(context, "src");
		BufferRange dst    = *GetInput<BufferParameter>(context, "dst");
		uint32_t    offset = GetInput<ConstantParameter>(context, "offset")->get<uint32_t>();
		uint32_t    value  = GetInput<ConstantParameter>(context, "value")->get<uint32_t>();
		context.cmd.Copy(src, dst);
		context.cmd.Fill(src.cast<uint32_t>(), value, offset);
		SetOutput(context, "dst", dst);
	}
};


}