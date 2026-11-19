#include "catch.hpp"
#include "test_helpers.hpp"

using namespace duckdb;

static unique_ptr<DuckDB> OpenDatabase(const string &prefix) {
	auto config = GetTestConfig();
	config->options.system_object_prefix = prefix;
	return make_uniq<DuckDB>(nullptr, config.get());
}

TEST_CASE("System objects take their names from the configured prefix", "[api][system_object_prefix]") {
	auto db = OpenDatabase("serenedb_");
	Connection con(*db);
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE t (i INTEGER PRIMARY KEY, j VARCHAR)"));
	REQUIRE_NO_FAIL(con.Query("CREATE VIEW v AS SELECT i FROM t"));

	auto result = con.Query("SELECT table_name FROM serenedb_tables() WHERE NOT internal");
	REQUIRE(CHECK_COLUMN(result, 0, {"t"}));
	result = con.Query("SELECT table_name FROM serenedb_tables");
	REQUIRE(CHECK_COLUMN(result, 0, {"t"}));
	result = con.Query("SELECT column_name FROM serenedb_columns() WHERE table_name = 't' ORDER BY column_index");
	REQUIRE(CHECK_COLUMN(result, 0, {"i", "j"}));
	result = con.Query("SELECT view_name FROM serenedb_views");
	REQUIRE(CHECK_COLUMN(result, 0, {"v"}));
	result = con.Query("SELECT name FROM serenedb_settings() WHERE name = 'enable_profiling'");
	REQUIRE(CHECK_COLUMN(result, 0, {"enable_profiling"}));
	result = con.Query("SELECT name FROM serenedb_profiling_settings() WHERE name = 'enable_profiling'");
	REQUIRE(CHECK_COLUMN(result, 0, {"enable_profiling"}));
	result =
	    con.Query("SELECT DISTINCT function_name FROM serenedb_functions() WHERE function_name IN ('serenedb_tables', "
	              "'truncate_serenedb_logs', 'parse_serenedb_log_message', 'serenedb_logs_parsed') ORDER BY 1");
	REQUIRE(CHECK_COLUMN(
	    result, 0,
	    {"parse_serenedb_log_message", "serenedb_logs_parsed", "serenedb_tables", "truncate_serenedb_logs"}));
	result = con.Query("SELECT examples FROM serenedb_functions() WHERE function_name = 'parse_serenedb_log_message'");
	REQUIRE(CHECK_COLUMN(result, 0, {Value::LIST({"parse_serenedb_log_message('FileSystem', log_message)"})}));

	result = con.Query("SELECT table_name FROM information_schema.tables WHERE table_schema = 'main' ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {"t", "v"}));
	result = con.Query("SELECT column_name FROM information_schema.columns WHERE table_name = 't' ORDER BY "
	                   "ordinal_position");
	REQUIRE(CHECK_COLUMN(result, 0, {"i", "j"}));
	result = con.Query("SELECT count(*) FROM information_schema.table_constraints WHERE table_name = 't' AND "
	                   "constraint_type = 'PRIMARY KEY'");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT relname FROM pg_catalog.pg_class WHERE relname IN ('t', 'v') ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {"t", "v"}));
	result = con.Query("SELECT pg_get_viewdef(oid) IS NOT NULL FROM pg_catalog.pg_class WHERE relname = 'v'");
	REQUIRE(CHECK_COLUMN(result, 0, {true}));
	REQUIRE_NO_FAIL(con.Query("SELECT pg_get_constraintdef(oid) FROM pg_catalog.pg_constraint"));
	REQUIRE_NO_FAIL(con.Query("SELECT format_type(oid, -1) FROM pg_catalog.pg_type"));
	result = con.Query("SELECT name FROM sqlite_master ORDER BY 1");
	REQUIRE(CHECK_COLUMN(result, 0, {"t", "v"}));
	result = con.Query("SELECT name FROM pragma_database_list");
	REQUIRE(CHECK_COLUMN(result, 0, {"memory"}));

	result = con.Query("SHOW TABLES");
	REQUIRE(CHECK_COLUMN(result, 0, {"t", "v"}));
	result = con.Query("SHOW TABLES FROM main");
	REQUIRE(CHECK_COLUMN(result, 0, {"t", "v"}));
	result = con.Query("SHOW ALL TABLES");
	REQUIRE(CHECK_COLUMN(result, 2, {"t", "v"}));
	result = con.Query("SHOW DATABASES");
	REQUIRE(CHECK_COLUMN(result, 0, {"memory"}));
	result = con.Query("SHOW SCHEMAS");
	REQUIRE(CHECK_COLUMN(result, 1, {"main"}));
	result = con.Query("PRAGMA show_tables");
	REQUIRE(CHECK_COLUMN(result, 0, {"t", "v"}));
	result = con.Query("DESCRIBE t");
	REQUIRE(CHECK_COLUMN(result, 0, {"i", "j"}));
	result = con.Query("PRAGMA table_info('t')");
	REQUIRE(CHECK_COLUMN(result, 1, {"i", "j"}));
	REQUIRE_NO_FAIL(con.Query("PRAGMA functions"));
	REQUIRE_NO_FAIL(con.Query("PRAGMA extension_versions"));
	REQUIRE_NO_FAIL(con.Query("SHOW VARIABLES"));
	REQUIRE_NO_FAIL(con.Query("SHOW EXTERNAL RESOURCES"));

	REQUIRE_NO_FAIL(con.Query("CALL enable_logging('QueryLog')"));
	REQUIRE_NO_FAIL(con.Query("SELECT 'hallo' AS value"));
	result = con.Query("SELECT message FROM serenedb_logs_parsed('QueryLog') WHERE message LIKE 'SELECT ''hallo''%'");
	REQUIRE(CHECK_COLUMN(result, 0, {"SELECT 'hallo' AS value"}));
	result = con.Query("SELECT count(*) FROM serenedb_logs WHERE message = 'SELECT ''hallo'' AS value'");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT count(*) FROM serenedb_logs() WHERE message = 'SELECT ''hallo'' AS value'");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	REQUIRE_NO_FAIL(con.Query("CALL truncate_serenedb_logs()"));
	result = con.Query("SELECT count(*) FROM serenedb_logs() WHERE message = 'SELECT ''hallo'' AS value'");
	REQUIRE(CHECK_COLUMN(result, 0, {0}));
	REQUIRE_NO_FAIL(con.Query("CALL disable_logging()"));

	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_tables()"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_tables"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_columns()"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_settings()"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_functions()"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_logs"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_logs_parsed('QueryLog')"));
	REQUIRE_FAIL(con.Query("CALL truncate_duckdb_logs()"));
	REQUIRE_FAIL(con.Query("SELECT parse_duckdb_log_message('QueryLog', 'x')"));
	result = con.Query("SELECT function_name FROM serenedb_functions() WHERE function_name ILIKE '%duckdb\\_%' ESCAPE "
	                   "'\\'");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
	result = con.Query("SELECT view_name FROM serenedb_views() WHERE view_name ILIKE '%duckdb\\_%' ESCAPE '\\'");
	REQUIRE(CHECK_COLUMN(result, 0, {}));
}

TEST_CASE("Databases with different system object prefixes coexist", "[api][system_object_prefix]") {
	auto serene = OpenDatabase("serenedb_");
	auto duck = OpenDatabase("duckdb_");
	Connection serene_con(*serene);
	Connection duck_con(*duck);
	for (auto con : {&serene_con, &duck_con}) {
		REQUIRE_NO_FAIL(con->Query("CREATE TABLE t (i INTEGER)"));
	}
	auto result = serene_con.Query("SELECT table_name FROM serenedb_tables");
	REQUIRE(CHECK_COLUMN(result, 0, {"t"}));
	result = duck_con.Query("SELECT table_name FROM duckdb_tables");
	REQUIRE(CHECK_COLUMN(result, 0, {"t"}));
	result = serene_con.Query("SELECT table_name FROM information_schema.tables");
	REQUIRE(CHECK_COLUMN(result, 0, {"t"}));
	result = duck_con.Query("SELECT table_name FROM information_schema.tables");
	REQUIRE(CHECK_COLUMN(result, 0, {"t"}));
	REQUIRE_FAIL(serene_con.Query("SELECT * FROM duckdb_tables"));
	REQUIRE_FAIL(duck_con.Query("SELECT * FROM serenedb_tables"));
}

TEST_CASE("The SQL formatter takes the system object prefix", "[api][system_object_prefix]") {
	auto db = OpenDatabase("serenedb_");
	if (!db->ExtensionIsLoaded("autocomplete")) {
		return;
	}
	Connection con(*db);
	auto result = con.Query("SELECT serenedb_format_sql('select 1')");
	REQUIRE(CHECK_COLUMN(result, 0, {"SELECT 1"}));
	result = con.Query("SELECT serenedb_format_sql('select 1', MAP {'keyword_case': 'shout'})");
	REQUIRE(result->HasError());
	REQUIRE(StringUtil::Contains(result->GetError(), "serenedb_format_sql: keyword_case"));
	result = con.Query("SELECT duckdb_format_sql('select 1')");
	REQUIRE(result->HasError());
	REQUIRE(StringUtil::Contains(result->GetError(), "duckdb_format_sql does not exist!\nDid you mean"));
	REQUIRE(!StringUtil::Contains(result->GetError(), "autocomplete extension"));
}

TEST_CASE("Log files take the system object prefix", "[api][system_object_prefix]") {
	auto db = OpenDatabase("serenedb_");
	Connection con(*db);
	auto dir = TestCreatePath("system_object_prefix_logs");
	REQUIRE_NO_FAIL(con.Query("CALL enable_logging(['QueryLog'], storage='file', storage_config={'path': '" + dir +
	                          "', 'normalize': true})"));
	REQUIRE_NO_FAIL(con.Query("SELECT 42"));
	auto result = con.Query("SELECT count(*) > 0 FROM read_csv('" + dir + "/serenedb_log_entries.csv')");
	REQUIRE(CHECK_COLUMN(result, 0, {true}));
	result = con.Query("SELECT count(*) > 0 FROM read_csv('" + dir + "/serenedb_log_contexts.csv')");
	REQUIRE(CHECK_COLUMN(result, 0, {true}));
	result = con.Query("SELECT count(*) FROM glob('" + dir + "/duckdb_*')");
	REQUIRE(CHECK_COLUMN(result, 0, {0}));
}

TEST_CASE("The user agent and the version name the configured product", "[api][system_object_prefix]") {
	auto config = GetTestConfig();
	config->options.product_name = "SereneDB";
	config->options.product_version = "26.11.0";
	config->options.product_source_id = "3f9c2a7d41";
	DuckDB db(nullptr, config.get());
	Connection con(db);
	auto result = con.Query("SELECT starts_with(user_agent, 'serenedb/26.11.0(') FROM pragma_user_agent()");
	REQUIRE(CHECK_COLUMN(result, 0, {true}));
	result = con.Query("SELECT library_version, source_id, codename FROM pragma_version()");
	REQUIRE(CHECK_COLUMN(result, 0, {"26.11.0"}));
	REQUIRE(CHECK_COLUMN(result, 1, {"3f9c2a7d41"}));
	REQUIRE(CHECK_COLUMN(result, 2, {Value()}));
	result = con.Query("SELECT version()");
	REQUIRE(CHECK_COLUMN(result, 0, {"26.11.0"}));
}

TEST_CASE("An application can turn extension management off", "[api][system_object_prefix]") {
	auto config = GetTestConfig();
	config->options.manage_extensions = false;
	DuckDB db(nullptr, config.get());
	Connection con(db);
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_extensions()"));
	REQUIRE_FAIL(con.Query("SELECT * FROM duckdb_extension_repositories()"));
	REQUIRE_FAIL(con.Query("PRAGMA extension_versions"));
	auto result = con.Query("CREATE EXTENSION REPOSITORY my_repo PREFIX 'https://example.com'");
	REQUIRE(result->HasError());
	REQUIRE(StringUtil::Contains(result->GetError(), "CREATE EXTENSION REPOSITORY is not supported"));
	result = con.Query("DROP EXTENSION REPOSITORY my_repo");
	REQUIRE(result->HasError());
	REQUIRE(StringUtil::Contains(result->GetError(), "DROP EXTENSION REPOSITORY is not supported"));
	REQUIRE_NO_FAIL(con.Query("SELECT * FROM duckdb_functions()"));
}

TEST_CASE("Without a configured product the version is the library's", "[api][system_object_prefix]") {
	DuckDB db(nullptr);
	Connection con(db);
	auto result = con.Query("SELECT library_version = version(), source_id IS NOT NULL, codename IS NOT NULL "
	                        "FROM pragma_version()");
	REQUIRE(CHECK_COLUMN(result, 0, {true}));
	REQUIRE(CHECK_COLUMN(result, 1, {true}));
	REQUIRE(CHECK_COLUMN(result, 2, {true}));
}
