#include "duckdb/parser/parser.hpp"
#include "duckdb/catalog/default/default_views.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/main/config.hpp"

#include <absl/strings/str_cat.h>

namespace duckdb {

struct DefaultView {
	const char *schema;
	const char *name;
	bool system_object;
	const char *sql;
	const char *references[8];
};

static const DefaultView internal_views[] = {
    {DEFAULT_SCHEMA,
     "pragma_database_list",
     false,
     "SELECT database_oid AS seq, database_name AS name, path AS file FROM %s() WHERE NOT internal ORDER BY 1",
     {"databases"}},
    {DEFAULT_SCHEMA,
     "sqlite_master",
     false,
     "select 'table' \"type\", table_name \"name\", table_name \"tbl_name\", 0 rootpage, sql from %s union all select "
     "'view' \"type\", view_name \"name\", view_name \"tbl_name\", 0 rootpage, sql from %s union all select 'index' "
     "\"type\", index_name \"name\", table_name \"tbl_name\", 0 rootpage, sql from %s;",
     {"tables", "views", "indexes"}},
    {DEFAULT_SCHEMA, "sqlite_schema", false, "SELECT * FROM sqlite_master", {}},
    {DEFAULT_SCHEMA, "sqlite_temp_master", false, "SELECT * FROM sqlite_master", {}},
    {DEFAULT_SCHEMA, "sqlite_temp_schema", false, "SELECT * FROM sqlite_master", {}},
    {DEFAULT_SCHEMA, "constraints", true, "SELECT * FROM %s()", {"constraints"}},
    {DEFAULT_SCHEMA, "columns", true, "SELECT * FROM %s() WHERE NOT internal", {"columns"}},
    {DEFAULT_SCHEMA, "databases", true, "SELECT * FROM %s() WHERE NOT internal", {"databases"}},
    {DEFAULT_SCHEMA, "indexes", true, "SELECT * FROM %s()", {"indexes"}},
    {DEFAULT_SCHEMA, "schemas", true, "SELECT * FROM %s() WHERE NOT internal", {"schemas"}},
    {DEFAULT_SCHEMA, "tables", true, "SELECT * FROM %s() WHERE NOT internal", {"tables"}},
    {DEFAULT_SCHEMA, "types", true, "SELECT * FROM %s()", {"types"}},
    {DEFAULT_SCHEMA, "views", true, "SELECT * FROM %s() WHERE NOT internal", {"views"}},
    {DEFAULT_SCHEMA, "logs", true, "SELECT * FROM %s(denormalized_table=true)", {"logs"}},
    {"pg_catalog", "pg_am", false, "SELECT 0 oid, 'art' amname, NULL amhandler, 'i' amtype", {}},
    {"pg_catalog",
     "pg_prepared_statements",
     false,
     "SELECT name, statement, NULL prepare_time, parameter_types, result_types, NULL from_sql, NULL generic_plans, "
     "NULL custom_plans from %s()",
     {"prepared_statements"}},
    {"pg_catalog",
     "pg_attribute",
     false,
     "SELECT table_oid attrelid, column_name attname, data_type_id atttypid, 0 attstattarget, NULL attlen, "
     "column_index attnum, 0 attndims, -1 attcacheoff, case when data_type ilike '%%decimal%%' then "
     "numeric_precision*1000+numeric_scale else -1 end atttypmod, false attbyval, NULL attstorage, NULL attalign, NOT "
     "is_nullable attnotnull, column_default IS NOT NULL atthasdef, false atthasmissing, '' attidentity, '' "
     "attgenerated, false attisdropped, true attislocal, 0 attinhcount, 0 attcollation, NULL attcompression, NULL "
     "attacl, NULL attoptions, NULL attfdwoptions, NULL attmissingval FROM %s()",
     {"columns"}},
    {"pg_catalog",
     "pg_attrdef",
     false,
     "SELECT column_index oid, table_oid adrelid, column_index adnum, column_default adbin from %s() where "
     "column_default is not null;",
     {"columns"}},
    {"pg_catalog",
     "pg_class",
     false,
     "SELECT table_oid oid, table_name relname, schema_oid relnamespace, 0 reltype, 0 reloftype, 0 relowner, 0 relam, "
     "0 relfilenode, 0 reltablespace, 0 relpages, estimated_size::real reltuples, 0 relallvisible, 0 reltoastrelid, 0 "
     "reltoastidxid, index_count > 0 relhasindex, false relisshared, case when temporary then 't' else 'p' end "
     "relpersistence, 'r' relkind, column_count relnatts, check_constraint_count relchecks, false relhasoids, "
     "has_primary_key relhaspkey, false relhasrules, false relhastriggers, false relhassubclass, false relrowsecurity, "
     "true relispopulated, NULL relreplident, false relispartition, 0 relrewrite, 0 relfrozenxid, NULL relminmxid, "
     "NULL relacl, NULL reloptions, NULL relpartbound FROM %s() UNION ALL SELECT view_oid oid, view_name "
     "relname, schema_oid relnamespace, 0 reltype, 0 reloftype, 0 relowner, 0 relam, 0 relfilenode, 0 reltablespace, 0 "
     "relpages, 0 reltuples, 0 relallvisible, 0 reltoastrelid, 0 reltoastidxid, false relhasindex, false relisshared, "
     "case when temporary then 't' else 'p' end relpersistence, 'v' relkind, column_count relnatts, 0 relchecks, false "
     "relhasoids, false relhaspkey, false relhasrules, false relhastriggers, false relhassubclass, false "
     "relrowsecurity, true relispopulated, NULL relreplident, false relispartition, 0 relrewrite, 0 relfrozenxid, NULL "
     "relminmxid, NULL relacl, NULL reloptions, NULL relpartbound FROM %s() UNION ALL SELECT sequence_oid "
     "oid, sequence_name relname, schema_oid relnamespace, 0 reltype, 0 reloftype, 0 relowner, 0 relam, 0 relfilenode, "
     "0 reltablespace, 0 relpages, 0 reltuples, 0 relallvisible, 0 reltoastrelid, 0 reltoastidxid, false relhasindex, "
     "false relisshared, case when temporary then 't' else 'p' end relpersistence, 'S' relkind, 0 relnatts, 0 "
     "relchecks, false relhasoids, false relhaspkey, false relhasrules, false relhastriggers, false relhassubclass, "
     "false relrowsecurity, true relispopulated, NULL relreplident, false relispartition, 0 relrewrite, 0 "
     "relfrozenxid, NULL relminmxid, NULL relacl, NULL reloptions, NULL relpartbound FROM %s() UNION ALL "
     "SELECT index_oid oid, index_name relname, schema_oid relnamespace, 0 reltype, 0 reloftype, 0 relowner, 0 relam, "
     "0 relfilenode, 0 reltablespace, 0 relpages, 0 reltuples, 0 relallvisible, 0 reltoastrelid, 0 reltoastidxid, "
     "false relhasindex, false relisshared, 't' relpersistence, 'i' relkind, NULL relnatts, 0 relchecks, false "
     "relhasoids, false relhaspkey, false relhasrules, false relhastriggers, false relhassubclass, false "
     "relrowsecurity, true relispopulated, NULL relreplident, false relispartition, 0 relrewrite, 0 relfrozenxid, NULL "
     "relminmxid, NULL relacl, NULL reloptions, NULL relpartbound FROM %s()",
     {"tables", "views", "sequences", "indexes"}},
    {"pg_catalog",
     "pg_constraint",
     false,
     "SELECT table_oid*1000000+constraint_index oid, constraint_text conname, schema_oid connamespace, CASE "
     "constraint_type WHEN 'CHECK' then 'c' WHEN 'UNIQUE' then 'u' WHEN 'PRIMARY KEY' THEN 'p' WHEN 'FOREIGN KEY' THEN "
     "'f' ELSE 'x' END contype, constraint_check_mode = 'DEFERRED' condeferrable, constraint_check_mode = 'DEFERRED' "
     "condeferred, true convalidated, table_oid "
     "conrelid, 0 "
     "contypid, 0 conindid, 0 conparentid, 0 confrelid, NULL confupdtype, NULL confdeltype, NULL confmatchtype, true "
     "conislocal, 0 coninhcount, false connoinherit, constraint_column_indexes conkey, NULL confkey, NULL conpfeqop, "
     "NULL conppeqop, NULL conffeqop, NULL conexclop, expression conbin FROM %s()",
     {"constraints"}},
    {"pg_catalog", "pg_collation", false, "SELECT NULL::OID oid, NULL::VARCHAR collname WHERE FALSE", {}},
    {"pg_catalog",
     "pg_database",
     false,
     "SELECT database_oid oid, database_name datname, true::boolean datallowconn, false::boolean AS datistemplate FROM "
     "%s()",
     {"databases"}},
    {"pg_catalog", "pg_depend", false, "SELECT * FROM %s()", {"dependencies"}},
    {"pg_catalog",
     "pg_description",
     false,
     "SELECT table_oid AS objoid, database_oid AS classoid, 0 AS objsubid, comment AS description FROM %s() "
     "WHERE NOT internal UNION ALL SELECT table_oid AS objoid, database_oid AS classoid, column_index AS objsubid, "
     "comment AS description FROM %s() WHERE NOT internal UNION ALL SELECT view_oid AS objoid, "
     "database_oid AS classoid, 0 AS objsubid, comment AS description FROM %s() WHERE NOT internal UNION ALL "
     "SELECT index_oid AS objoid, database_oid AS classoid, 0 AS objsubid, comment AS description FROM %s "
     "UNION ALL SELECT sequence_oid AS objoid, database_oid AS classoid, 0 AS objsubid, comment AS description FROM "
     "%s() UNION ALL SELECT type_oid AS objoid, database_oid AS classoid, 0 AS objsubid, comment AS "
     "description FROM %s() WHERE NOT internal UNION ALL SELECT function_oid AS objoid, database_oid AS "
     "classoid, 0 AS objsubid, comment AS description FROM %s() WHERE NOT internal;",
     {"tables", "columns", "views", "indexes", "sequences", "types", "functions"}},
    {"pg_catalog",
     "pg_enum",
     false,
     "SELECT NULL oid, a.type_oid enumtypid, list_position(b.labels, a.elabel) enumsortorder, a.elabel enumlabel FROM "
     "(SELECT UNNEST(labels) elabel, type_oid FROM %s() WHERE logical_type='ENUM') a JOIN %s() b "
     "ON a.type_oid=b.type_oid;",
     {"types", "types"}},
    {"pg_catalog",
     "pg_index",
     false,
     "SELECT index_oid indexrelid, table_oid indrelid, 0 indnatts, 0 indnkeyatts, is_unique indisunique, is_primary "
     "indisprimary, false indisexclusion, true indimmediate, false indisclustered, true indisvalid, false "
     "indcheckxmin, true indisready, true indislive, false indisreplident, NULL::INT[] indkey, NULL::OID[] "
     "indcollation, NULL::OID[] indclass, NULL::INT[] indoption, expressions indexprs, NULL indpred FROM "
     "%s()",
     {"indexes"}},
    {"pg_catalog",
     "pg_indexes",
     false,
     "SELECT schema_name schemaname, table_name tablename, index_name indexname, NULL \"tablespace\", sql indexdef "
     "FROM %s()",
     {"indexes"}},
    {"pg_catalog",
     "pg_namespace",
     false,
     "SELECT oid, schema_name nspname, 0 nspowner, NULL nspacl FROM %s() where "
     "database_name=current_database()",
     {"schemas"}},
    {"pg_catalog",
     "pg_proc",
     false,
     "SELECT f.function_oid oid, function_name proname, s.oid pronamespace,  NULL proowner, NULL prolang, 0 procost, 0 "
     "prorows, varargs provariadic,  0 prosupport, CASE function_type WHEN 'aggregate' THEN 'a' ELSE 'f' END prokind, "
     "false prosecdef, false proleakproof, false proisstrict, function_type = 'table' proretset,  case (stability) "
     "when 'CONSISTENT' then 'i' when 'CONSISTENT_WITHIN_QUERY' then 's' when 'VOLATILE' then 'v' end provolatile, 'u' "
     "proparallel, length(parameters)  pronargs, 0 pronargdefaults, return_type prorettype,  parameter_types "
     "proargtypes,  NULL proallargtypes, NULL proargmodes, parameters proargnames, NULL proargdefaults, NULL "
     "protrftypes, NULL prosrc, NULL probin, macro_definition prosqlbody, NULL proconfig, NULL proacl, function_type = "
     "'aggregate' proisagg,  FROM %s() f LEFT JOIN %s() s USING (database_name, "
     "schema_name)",
     {"functions", "schemas"}},
    {"pg_catalog",
     "pg_sequence",
     false,
     "SELECT sequence_oid seqrelid, 0 seqtypid, start_value seqstart, increment_by seqincrement, max_value seqmax, "
     "min_value seqmin, 0 seqcache, cycle seqcycle FROM %s()",
     {"sequences"}},
    {"pg_catalog",
     "pg_sequences",
     false,
     "SELECT schema_name schemaname, sequence_name sequencename, 'duckdb' sequenceowner, 0 data_type, start_value, "
     "min_value, max_value, increment_by, cycle, 0 cache_size, last_value FROM %s()",
     {"sequences"}},
    {"pg_catalog",
     "pg_settings",
     false,
     "SELECT name, value setting, description short_desc, CASE WHEN input_type = 'VARCHAR' THEN 'string' WHEN "
     "input_type = 'BOOLEAN' THEN 'bool' WHEN input_type IN ('BIGINT', 'UBIGINT') THEN 'integer' ELSE input_type END "
     "vartype FROM %s()",
     {"settings"}},
    {"pg_catalog",
     "pg_tables",
     false,
     "SELECT schema_name schemaname, table_name tablename, 'duckdb' tableowner, NULL \"tablespace\", index_count > 0 "
     "hasindexes, false hasrules, false hastriggers FROM %s()",
     {"tables"}},
    {"pg_catalog",
     "pg_tablespace",
     false,
     "SELECT 0 oid, 'pg_default' spcname, 0 spcowner, NULL spcacl, NULL spcoptions",
     {}},
    {"pg_catalog",
     "pg_type",
     false,
     "SELECT CASE WHEN type_oid IS NULL THEN NULL WHEN logical_type = 'ENUM' AND type_name <> 'enum' THEN type_oid "
     "ELSE map_to_pg_oid(type_name) END oid, format_pg_type(logical_type, type_name) typname, schema_oid typnamespace, "
     "0 typowner, type_size typlen, false typbyval, CASE WHEN logical_type='ENUM' THEN 'e' else 'b' end typtype, CASE "
     "WHEN type_category='NUMERIC' THEN 'N' WHEN type_category='STRING' THEN 'S' WHEN type_category='DATETIME' THEN "
     "'D' WHEN type_category='BOOLEAN' THEN 'B' WHEN type_category='COMPOSITE' THEN 'C' WHEN type_category='USER' THEN "
     "'U' ELSE 'X' END typcategory, false typispreferred, true typisdefined, NULL typdelim, NULL typrelid, NULL "
     "typsubscript, NULL typelem, NULL typarray, NULL typinput, NULL typoutput, NULL typreceive, NULL typsend, NULL "
     "typmodin, NULL typmodout, NULL typanalyze, 'd' typalign, 'p' typstorage, NULL typnotnull, NULL typbasetype, NULL "
     "typtypmod, NULL typndims, NULL typcollation, NULL typdefaultbin, NULL typdefault, NULL typacl FROM "
     "%s() WHERE type_oid IS NOT NULL;",
     {"types"}},
    {"pg_catalog",
     "pg_views",
     false,
     "SELECT schema_name schemaname, view_name viewname, 'duckdb' viewowner, sql definition FROM %s()",
     {"views"}},
    {"information_schema",
     "columns",
     false,
     "SELECT database_name table_catalog, schema_name table_schema, table_name, column_name, column_index "
     "ordinal_position, column_default, CASE WHEN is_nullable THEN 'YES' ELSE 'NO' END is_nullable, data_type, "
     "character_maximum_length, NULL::INT character_octet_length, numeric_precision, numeric_precision_radix, "
     "numeric_scale, NULL::INT datetime_precision, NULL::VARCHAR interval_type, NULL::INT interval_precision, "
     "NULL::VARCHAR character_set_catalog, NULL::VARCHAR character_set_schema, NULL::VARCHAR character_set_name, "
     "NULL::VARCHAR collation_catalog, NULL::VARCHAR collation_schema, NULL::VARCHAR collation_name, NULL::VARCHAR "
     "domain_catalog, NULL::VARCHAR domain_schema, NULL::VARCHAR domain_name, NULL::VARCHAR udt_catalog, NULL::VARCHAR "
     "udt_schema, NULL::VARCHAR udt_name, NULL::VARCHAR scope_catalog, NULL::VARCHAR scope_schema, NULL::VARCHAR "
     "scope_name, NULL::BIGINT maximum_cardinality, NULL::VARCHAR dtd_identifier, NULL::BOOL is_self_referencing, "
     "NULL::BOOL is_identity, NULL::VARCHAR identity_generation, NULL::VARCHAR identity_start, NULL::VARCHAR "
     "identity_increment, NULL::VARCHAR identity_maximum, NULL::VARCHAR identity_minimum, NULL::BOOL identity_cycle, "
     "CASE WHEN is_generated THEN 'ALWAYS' ELSE 'NEVER' END is_generated, generation_expression, NULL::BOOL "
     "is_updatable, comment AS COLUMN_COMMENT FROM %s;",
     {"columns"}},
    {"information_schema",
     "schemata",
     false,
     "SELECT database_name catalog_name, schema_name, 'duckdb' schema_owner, NULL::VARCHAR "
     "default_character_set_catalog, NULL::VARCHAR default_character_set_schema, NULL::VARCHAR "
     "default_character_set_name, sql sql_path FROM %s()",
     {"schemas"}},
    {"information_schema",
     "tables",
     false,
     "SELECT database_name table_catalog, schema_name table_schema, table_name, CASE WHEN temporary THEN 'LOCAL "
     "TEMPORARY' ELSE 'BASE TABLE' END table_type, NULL::VARCHAR self_referencing_column_name, NULL::VARCHAR "
     "reference_generation, NULL::VARCHAR user_defined_type_catalog, NULL::VARCHAR user_defined_type_schema, "
     "NULL::VARCHAR user_defined_type_name, 'YES' is_insertable_into, 'NO' is_typed, CASE WHEN temporary THEN "
     "'PRESERVE' ELSE NULL END commit_action, comment AS TABLE_COMMENT FROM %s() UNION ALL SELECT "
     "database_name table_catalog, schema_name table_schema, view_name table_name, 'VIEW' table_type, NULL "
     "self_referencing_column_name, NULL reference_generation, NULL user_defined_type_catalog, NULL "
     "user_defined_type_schema, NULL user_defined_type_name, 'NO' is_insertable_into, 'NO' is_typed, NULL "
     "commit_action, comment AS TABLE_COMMENT FROM %s;",
     {"tables", "views"}},
    {"information_schema",
     "character_sets",
     false,
     "SELECT NULL::VARCHAR character_set_catalog, NULL::VARCHAR character_set_schema, 'UTF8' character_set_name, 'UCS' "
     "character_repertoire, 'UTF8' form_of_use, current_database() default_collate_catalog, 'pg_catalog' "
     "default_collate_schema, 'ucs_basic' default_collate_name;",
     {}},
    {"information_schema",
     "referential_constraints",
     false,
     "SELECT f.database_name constraint_catalog, f.schema_name constraint_schema, f.constraint_name constraint_name, "
     "c.database_name unique_constraint_catalog, c.schema_name unique_constraint_schema, c.constraint_name "
     "unique_constraint_name, 'NONE' match_option, 'NO ACTION' update_rule, 'NO ACTION' delete_rule FROM "
     "%s() c, %s() f WHERE f.constraint_type = 'FOREIGN KEY' AND (c.constraint_type = "
     "'UNIQUE' OR c.constraint_type = 'PRIMARY KEY') AND f.database_oid = c.database_oid AND f.schema_oid = "
     "c.schema_oid AND lower(f.referenced_table) = lower(c.table_name) AND [lower(x) for x in "
     "f.referenced_column_names] = [lower(x) for x in c.constraint_column_names]",
     {"constraints", "constraints"}},
    {"information_schema",
     "key_column_usage",
     false,
     "SELECT database_name constraint_catalog, schema_name constraint_schema, constraint_name, database_name "
     "table_catalog, schema_name table_schema, table_name, UNNEST(constraint_column_names) column_name, "
     "UNNEST(generate_series(1, len(constraint_column_names))) ordinal_position, CASE constraint_type WHEN 'FOREIGN "
     "KEY' THEN UNNEST (generate_series(1, len(constraint_column_names))) ELSE NULL END position_in_unique_constraint "
     "FROM %s() WHERE constraint_type = 'FOREIGN KEY' OR constraint_type = 'PRIMARY KEY' OR "
     "constraint_type = 'UNIQUE';",
     {"constraints"}},
    {"information_schema",
     "table_constraints",
     false,
     "SELECT database_name constraint_catalog, schema_name constraint_schema, constraint_name, database_name "
     "table_catalog, schema_name table_schema, table_name, CASE constraint_type WHEN 'NOT NULL' THEN 'CHECK' ELSE "
     "constraint_type END constraint_type, CASE WHEN constraint_check_mode = 'DEFERRED' THEN 'YES' ELSE 'NO' END "
     "is_deferrable, CASE WHEN "
     "constraint_check_mode = 'DEFERRED' THEN 'YES' ELSE 'NO' END initially_deferred, 'YES' enforced, 'YES' "
     "nulls_distinct FROM %s() WHERE constraint_type = 'PRIMARY KEY' OR constraint_type = 'FOREIGN "
     "KEY' OR constraint_type = 'UNIQUE' OR constraint_type = 'CHECK' OR constraint_type = 'NOT NULL';",
     {"constraints"}},
    {"information_schema",
     "constraint_column_usage",
     false,
     "SELECT database_name AS table_catalog, schema_name AS table_schema, table_name, column_name, database_name AS "
     "constraint_catalog, schema_name AS constraint_schema, constraint_name, constraint_type, constraint_text FROM "
     "(SELECT dc.*, UNNEST(dc.constraint_column_names) AS column_name FROM %s() AS dc WHERE "
     "constraint_type NOT IN ('NOT NULL') );",
     {"constraints"}},
    {"information_schema",
     "constraint_table_usage",
     false,
     "SELECT database_name AS table_catalog, schema_name AS table_schema, table_name, database_name AS "
     "constraint_catalog, schema_name AS constraint_schema, constraint_name, constraint_type FROM %s() "
     "WHERE constraint_type NOT IN ('NOT NULL');",
     {"constraints"}},
    {"information_schema",
     "check_constraints",
     false,
     "SELECT database_name AS constraint_catalog, schema_name AS constraint_schema, constraint_name, CASE "
     "constraint_type WHEN 'NOT NULL' THEN column_name || ' IS NOT NULL' ELSE constraint_text END AS check_clause FROM "
     "(SELECT dc.*, UNNEST(dc.constraint_column_names) AS column_name FROM %s() AS dc WHERE "
     "constraint_type IN ('CHECK', 'NOT NULL'));",
     {"constraints"}},
    {"information_schema",
     "views",
     false,
     "SELECT database_name AS table_catalog, schema_name AS table_schema, view_name AS table_name, sql AS "
     "view_definition, 'NONE' AS check_option, 'NO' AS is_updatable, 'NO' AS is_insertable_into, 'NO' AS "
     "is_trigger_updatable, 'NO' AS is_trigger_deletable, 'NO' AS is_trigger_insertable_into FROM %s();",
     {"views"}},
    {nullptr, nullptr, false, nullptr, {}}};

struct DefaultViewNames {
	using ViewKey = std::pair<std::string_view, std::string_view>;

	vector<string> names;
	absl::flat_hash_map<ViewKey, idx_t, CaseInsensitivePairHash, CaseInsensitivePairEquality> views;
};

namespace {

struct DefaultViewNameTables {
	mutex lock;
	unordered_map<string, unique_ptr<DefaultViewNames>> by_prefix;
};

const DefaultViewNames &ViewNamesFor(const string &prefix) {
	static auto &tables = *new DefaultViewNameTables();
	lock_guard<mutex> guard(tables.lock);
	auto &table = tables.by_prefix[prefix];
	if (!table) {
		table = make_uniq<DefaultViewNames>();
		for (idx_t index = 0; internal_views[index].name != nullptr; index++) {
			auto &view = internal_views[index];
			table->names.push_back(view.system_object ? absl::StrCat(prefix, view.name) : string(view.name));
		}
		for (idx_t index = 0; index < table->names.size(); index++) {
			table->views.emplace(DefaultViewNames::ViewKey(internal_views[index].schema, table->names[index]), index);
		}
	}
	return *table;
}

} // namespace

DefaultViewGenerator::DefaultViewGenerator(Catalog &catalog, SchemaCatalogEntry &schema)
    : DefaultGenerator(catalog), schema(schema),
      view_names(ViewNamesFor(DBConfig::GetConfig(catalog.GetDatabase()).options.system_object_prefix)) {
}

unique_ptr<CreateViewInfo> DefaultViewGenerator::GetDefaultView(const Identifier &input_schema,
                                                                const Identifier &input_name) const {
	auto &views = view_names.views;
	auto it = views.find(DefaultViewNames::ViewKey(input_schema.GetIdentifierName(), input_name.GetIdentifierName()));
	if (it == views.end()) {
		return nullptr;
	}
	auto &view = internal_views[it->second];
	auto result = make_uniq<CreateViewInfo>();
	result->SetQualifiedName(QualifiedName({input_schema}, input_name));
	result->sql = SystemObjectSQL(view.sql, view.references);
	result->temporary = true;
	result->internal = true;
	auto parser = Parser::GetBuiltinParser();
	return CreateViewInfo::FromSelect(parser, std::move(result));
}

bool DefaultViewGenerator::ServesViewsOf(const Identifier &view_schema) const {
	return (view_schema == DEFAULT_SCHEMA || catalog.IsDuckCatalog()) && ServesDefaults(schema.name, view_schema);
}

unique_ptr<CatalogEntry> DefaultViewGenerator::CreateDefaultEntry(ClientContext &context,
                                                                  const Identifier &entry_name) {
	unique_ptr<CreateViewInfo> info;
	if (ServesViewsOf(schema.name)) {
		info = GetDefaultView(schema.name, entry_name);
	}
	if (!info && ServesViewsOf(DEFAULT_SCHEMA)) {
		info = GetDefaultView(DEFAULT_SCHEMA, entry_name);
	}
	if (info) {
		return make_uniq_base<CatalogEntry, ViewCatalogEntry>(catalog, schema, *info);
	}
	return nullptr;
}

vector<Identifier> DefaultViewGenerator::GetDefaultEntries() {
	vector<Identifier> result;
	for (idx_t index = 0; index < view_names.names.size(); index++) {
		if (ServesViewsOf(internal_views[index].schema)) {
			result.emplace_back(view_names.names[index]);
		}
	}
	return result;
}

} // namespace duckdb
