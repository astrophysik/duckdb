#include "duckdb/common/tree_renderer/compact_tree_renderer.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/main/profiler/profiling_node.hpp"
#include "duckdb/main/query_profiler.hpp"
#include "duckdb/parallel/pipeline.hpp"
#include "duckdb/planner/logical_operator.hpp"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_replace.h>

namespace duckdb {

string CompactTreeRenderer::ToString(const LogicalOperator &op) {
	StringTreeRenderer ss;
	Render(op, ss);
	return ss.str();
}

string CompactTreeRenderer::ToString(const PhysicalOperator &op) {
	StringTreeRenderer ss;
	Render(op, ss);
	return ss.str();
}

string CompactTreeRenderer::ToString(const ProfilingNode &op) {
	StringTreeRenderer ss;
	Render(op, ss);
	return ss.str();
}

string CompactTreeRenderer::ToString(const Pipeline &op) {
	StringTreeRenderer ss;
	Render(op, ss);
	return ss.str();
}

void CompactTreeRenderer::Render(const LogicalOperator &op, BaseTreeRenderer &ss) {
	auto tree = RenderTree::CreateRenderTree(op);
	ToStream(*tree, ss);
}

void CompactTreeRenderer::Render(const PhysicalOperator &op, BaseTreeRenderer &ss) {
	auto tree = RenderTree::CreateRenderTree(op);
	ToStream(*tree, ss);
}

void CompactTreeRenderer::Render(const ProfilingNode &op, BaseTreeRenderer &ss) {
	auto tree = RenderTree::CreateRenderTree(op);
	ToStream(*tree, ss);
}

void CompactTreeRenderer::Render(const Pipeline &op, BaseTreeRenderer &ss) {
	auto tree = RenderTree::CreateRenderTree(op);
	ToStream(*tree, ss);
}

void CompactTreeRenderer::RenderProfiler(const QueryProfiler &profiler, BaseTreeRenderer &ss) {
	auto &metrics = profiler.GetQueryMetrics();
	vector<string> stats;
	stats.push_back(StringUtil::Format("time=%.4fs", metrics.GetStringMetricInSeconds("query.total_time")));
	auto bytes_read = metrics.GetBytesRead();
	if (bytes_read > 0) {
		stats.push_back("read=" + StringUtil::BytesToHumanReadableString(bytes_read, 1000));
	}
	auto bytes_written = metrics.GetBytesWritten();
	if (bytes_written > 0) {
		stats.push_back("written=" + StringUtil::BytesToHumanReadableString(bytes_written, 1000));
	}
	ss << "QUERY (" << StringUtil::Join(stats, ", ") << ")\n";
	profiler.RenderProfilingNodeTree(*this, ss);
}

void CompactTreeRenderer::ToStreamInternal(RenderTree &root, BaseTreeRenderer &ss) {
	RenderRecursive(root, ss, 0, 0, 0);
}

//! Collapse a (possibly multi-line) property value onto one line, quoted when it holds a separator, a brace or a quote
static string CompactValue(const string &value) {
	auto lines = StringUtil::Split(value, "\n");
	vector<string> parts;
	for (auto &line : lines) {
		auto trimmed = line;
		StringUtil::Trim(trimmed);
		if (!trimmed.empty()) {
			parts.push_back(std::move(trimmed));
		}
	}
	auto joined = StringUtil::Join(parts, ", ");
	if (joined.find_first_of(";{}\"") == string::npos) {
		return joined;
	}
	return absl::StrCat("\"", absl::StrReplaceAll(joined, {{"\\", "\\\\"}, {"\"", "\\\""}}), "\"");
}

static string CompactAttributes(const ExplainNode &node) {
	return absl::StrJoin(node.attributes, "; ", [](string *out, const auto &attr) {
		absl::StrAppend(out, attr.first, ": ", CompactValue(attr.second.ToString()));
	});
}

static void RenderExplainNode(const ExplainNode &node, BaseTreeRenderer &ss, idx_t depth) {
	ss << string(depth * 2, ' ') << node.label;
	if (!node.attributes.empty()) {
		ss << " {" << CompactAttributes(node) << "}";
	}
	ss << '\n';
	for (auto &child : node.children) {
		RenderExplainNode(child, ss, depth + 1);
	}
}

static void RenderStructured(const string &key, const ExplainValue &value, BaseTreeRenderer &ss, idx_t depth) {
	auto &node = value.Structured();
	ss << string(depth * 2, ' ') << key << ":";
	if (!value.IsSection()) {
		ss << '\n';
		RenderExplainNode(node, ss, depth + 1);
		return;
	}
	if (!node.attributes.empty()) {
		ss << " " << CompactAttributes(node);
	}
	ss << '\n';
	for (auto &child : node.children) {
		RenderExplainNode(child, ss, depth + 1);
	}
}

void CompactTreeRenderer::RenderRecursive(RenderTree &tree, BaseTreeRenderer &ss, idx_t depth, idx_t x, idx_t y) {
	auto node_p = tree.GetNode(x, y);
	D_ASSERT(node_p);
	auto &node = *node_p;

	// the row counts and timing go in parentheses right after the name, the remaining properties follow
	vector<string> stats;
	vector<string> properties;
	vector<pair<string, reference<const ExplainValue>>> structured;
	for (auto &entry : node.extra_text) {
		auto &key = entry.first;
		if (entry.second.empty()) {
			continue;
		}
		auto display_key = key;
		if (StringUtil::StartsWith(display_key, "__")) {
			display_key = StringUtil::Title(StringUtil::Replace(StringUtil::Replace(display_key, "__", ""), "_", " "));
		}
		if (entry.second.IsStructured()) {
			structured.emplace_back(std::move(display_key), entry.second);
			continue;
		}
		auto value = entry.second.ToString();
		if (key == RenderTreeNode::CARDINALITY) {
			stats.push_back("rows=" + value);
		} else if (key == RenderTreeNode::ESTIMATED_CARDINALITY) {
			stats.push_back("est=" + value);
		} else if (key == RenderTreeNode::TIMING) {
			stats.push_back("time=" + value);
		} else {
			properties.push_back(display_key + ": " + CompactValue(value));
		}
	}

	ss << string(depth * 2, ' ') << node.name;
	if (!stats.empty()) {
		ss << " (" << StringUtil::Join(stats, ", ") << ")";
	}
	if (!properties.empty()) {
		ss << " " << StringUtil::Join(properties, "; ");
	}
	ss << '\n';
	for (auto &entry : structured) {
		RenderStructured(entry.first, entry.second.get(), ss, depth + 1);
	}

	for (auto &child_pos : node.child_positions) {
		RenderRecursive(tree, ss, depth + 1, child_pos.x, child_pos.y);
	}
}

} // namespace duckdb
