#include "duckdb/execution/operator/helper/physical_prepare.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_data.hpp"

namespace duckdb {

SourceResultType PhysicalPrepare::GetDataInternal(ExecutionContext &context, DataChunk &chunk,
                                                  OperatorSourceInput &input) const {
	auto &client = context.client;

	// store the prepared statement in the context
	const std::string_view identifier(name.data(), name.size());
	auto entry = make_shared_ptr<PreparedStatementEntry>(prepared);
	entry->from_sql = !ClientContext::IsPreparedStatementHandle(Identifier(identifier));
	if (!ClientData::Get(client).prepared_statements.try_emplace(identifier, std::move(entry)).second) {
		throw CatalogException::DuplicatePreparedStatement(Identifier(identifier));
	}

	return SourceResultType::FINISHED;
}

} // namespace duckdb
