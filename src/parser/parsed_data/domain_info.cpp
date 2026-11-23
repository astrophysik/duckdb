#include "duckdb/parser/parsed_data/domain_info.hpp"

namespace duckdb {

bool DomainInfo::IsNotNull() const {
	for (auto &constraint : constraints) {
		if (constraint->type == ConstraintType::NOT_NULL) {
			return true;
		}
	}
	return false;
}

unique_ptr<DomainInfo> DomainInfo::Copy() const {
	auto result = make_uniq<DomainInfo>();
	result->default_value = default_value ? default_value->Copy() : nullptr;
	result->collation = collation;
	result->base_type = base_type;
	for (auto &constraint : constraints) {
		result->constraints.push_back(constraint->Copy());
	}
	return result;
}

} // namespace duckdb
