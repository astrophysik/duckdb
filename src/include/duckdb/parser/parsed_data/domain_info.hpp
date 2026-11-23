#pragma once

#include "duckdb/parser/constraint.hpp"
#include "duckdb/parser/parsed_expression.hpp"

namespace duckdb {

//! What CREATE DOMAIN adds to a type over its base type
struct DomainInfo {
	//! DEFAULT expression, if any
	unique_ptr<ParsedExpression> default_value;
	//! COLLATE name, empty if none
	string collation;
	//! NOT NULL and CHECK constraints in declaration order
	vector<unique_ptr<Constraint>> constraints;
	LogicalType base_type;

	//! Whether a NOT NULL constraint is present (pg_type.typnotnull)
	bool IsNotNull() const;

	unique_ptr<DomainInfo> Copy() const;
	void Serialize(Serializer &serializer) const;
	static unique_ptr<DomainInfo> Deserialize(Deserializer &deserializer);
};

} // namespace duckdb
