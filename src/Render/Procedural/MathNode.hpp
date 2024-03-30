#pragma once

#include "ProceduralNode.hpp"

namespace RoseEngine {

// This node is effectively a passthrough which 'compiles' to the given expression
class ExpressionNode : public ProceduralNode {
public:
	std::string value = "0";

	inline ExpressionNode(const std::string& value) : value(value) {}

	inline size_t hash() const override {
		return HashArgs(ProceduralNode::hash(), value);
	}

	inline NodeOutputMap Compile(ProceduralNodeCompiler& compiler) const override {
		return { {  NodeOutputConnection::gDefaultOutputName, value } };
	}

	void Gui(float width = 0) override;

	std::string Serialize() const override;
	void Deserialize(const std::string& serialized) override;
};

class MathNode : public ProceduralNode {
public:
	enum MathOp {
		eAdd,
		eSubtract,
		eMultiply,
		eDivide,
		ePow,
		eExp,
		eExp2,
		eLog,
		eLog2,
		eLog10,
		eSqrt,
		eStep,
		eMin,
		eMax,
		eRound,
		eFloor,
		eCeil,
		eFrac,
		eTrunc,
		eSin,
		eCos,
		eTan,
		eAsin,
		eAcos,
		eAtan,
		eAtan2,
		eSinh,
		eCosh,
		eTanh,
		eLerp,
		eClamp,
		eLength,
		eNormalize,
		eDot,
		eCross,
		eOpCount
	};
	inline static const char* GetOpName(MathOp op) {
		switch (op) {
			default: return ""; break;
			case MathOp::eAdd:       return "add";
			case MathOp::eSubtract:  return "sub";
			case MathOp::eMultiply:  return "mul";
			case MathOp::eDivide:    return "div";
			case MathOp::ePow:       return "pow";
			case MathOp::eExp:       return "exp";
			case MathOp::eExp2:      return "exp2";
			case MathOp::eLog:       return "log";
			case MathOp::eLog2:      return "log2";
			case MathOp::eLog10:     return "log10";
			case MathOp::eRound:     return "round";
			case MathOp::eSqrt:      return "sqrt";
			case MathOp::eStep:      return "step";
			case MathOp::eMin:       return "min";
			case MathOp::eMax:       return "max";
			case MathOp::eFloor:     return "floor";
			case MathOp::eCeil:      return "ceil";
			case MathOp::eFrac:      return "frac";
			case MathOp::eTrunc:     return "trunc";
			case MathOp::eSin:       return "sin";
			case MathOp::eCos:       return "cos";
			case MathOp::eTan:       return "tan";
			case MathOp::eAsin:      return "asin";
			case MathOp::eAcos:      return "acos";
			case MathOp::eAtan:      return "atan";
			case MathOp::eAtan2:     return "atan2";
			case MathOp::eSinh:      return "sinh";
			case MathOp::eCosh:      return "cosh";
			case MathOp::eTanh:      return "tanh";
			case MathOp::eLerp:      return "lerp";
			case MathOp::eClamp:     return "clamp";
			case MathOp::eLength:    return "length";
			case MathOp::eNormalize: return "normalize";
			case MathOp::eDot:       return "dot";
			case MathOp::eCross:     return "cross";
		}
	}
	inline static uint32_t GetArgCount(MathOp op) {
		switch (op) {
			default: return 0; break;
			case MathOp::eAdd:       return 2;
			case MathOp::eSubtract:  return 2;
			case MathOp::eMultiply:  return 2;
			case MathOp::eDivide:    return 2;
			case MathOp::ePow:       return 2;
			case MathOp::eExp:       return 1;
			case MathOp::eExp2:      return 1;
			case MathOp::eLog:       return 1;
			case MathOp::eLog2:      return 1;
			case MathOp::eLog10:     return 1;
			case MathOp::eRound:     return 1;
			case MathOp::eSqrt:      return 1;
			case MathOp::eStep:      return 2;
			case MathOp::eMin:       return 2;
			case MathOp::eMax:       return 2;
			case MathOp::eFloor:     return 1;
			case MathOp::eCeil:      return 1;
			case MathOp::eFrac:      return 1;
			case MathOp::eTrunc:     return 1;
			case MathOp::eSin:       return 1;
			case MathOp::eCos:       return 1;
			case MathOp::eTan:       return 1;
			case MathOp::eAsin:      return 1;
			case MathOp::eAcos:      return 1;
			case MathOp::eAtan:      return 1;
			case MathOp::eAtan2:     return 2;
			case MathOp::eSinh:      return 1;
			case MathOp::eCosh:      return 1;
			case MathOp::eTanh:      return 1;
			case MathOp::eLerp:      return 3;
			case MathOp::eClamp:     return 3;
			case MathOp::eLength:    return 1;
			case MathOp::eNormalize: return 1;
			case MathOp::eDot:       return 2;
			case MathOp::eCross:     return 2;
		}
	}

	MathOp op = MathOp::eAdd;

	inline MathNode(MathOp op_ = MathOp::eAdd, const NodeOutputConnection& a = {}, const NodeOutputConnection& b = {}, const NodeOutputConnection& c = {}) : op(op_) {
		inputs = {
			{ "a", a },
			{ "b", b },
			{ "c", c }
		};
	}

	inline size_t hash() const override {
		return HashArgs(ProceduralNode::hash(), op);
	}

	NodeOutputMap Compile(ProceduralNodeCompiler& compiler) const override;

	void Gui(float width = 0) override;

	std::string Serialize() const override;
	void Deserialize(const std::string& serialized) override;
};

};