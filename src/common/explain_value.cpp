#include "duckdb/common/explain_value.hpp"

#include "duckdb/common/string_util.hpp"

#include <absl/strings/str_cat.h>

namespace duckdb {

ExplainNode::ExplainNode(string label_p) : label(std::move(label_p)) {
}

ExplainValue::ExplainValue(ExplainNode node) : structured(make_shared_ptr<ExplainNode>(std::move(node))) {
}

ExplainValue ExplainValue::Timing(double seconds) {
	ExplainValue result(StringUtil::FormatTiming(seconds));
	result.timing = true;
	return result;
}

static void FlattenExplainNode(const ExplainNode &node, idx_t depth, string &result) {
	if (!result.empty()) {
		result += "\n";
	}
	string indent(depth * 2, ' ');
	result += indent + node.label;
	for (auto &attr : node.attributes) {
		absl::StrAppend(&result, "\n", indent, "  ", attr.first, ": ", attr.second.ToString());
	}
	for (auto &child : node.children) {
		FlattenExplainNode(child, depth + 1, result);
	}
}

string ExplainValue::ToString() const {
	if (!structured) {
		return scalar;
	}
	string result;
	FlattenExplainNode(*structured, 0, result);
	return result;
}

} // namespace duckdb
