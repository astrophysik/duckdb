//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/catalog/default/default_views.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/catalog/default/default_generator.hpp"

namespace duckdb {
class SchemaCatalogEntry;
struct DefaultViewNames;

class DefaultViewGenerator : public DefaultGenerator {
public:
	DefaultViewGenerator(Catalog &catalog, SchemaCatalogEntry &schema);

	SchemaCatalogEntry &schema;

public:
	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const Identifier &entry_name) override;
	vector<Identifier> GetDefaultEntries() override;

private:
	bool ServesViewsOf(const Identifier &view_schema) const;
	unique_ptr<CreateViewInfo> GetDefaultView(const Identifier &input_schema, const Identifier &input_name) const;

	const DefaultViewNames &view_names;
};

} // namespace duckdb
