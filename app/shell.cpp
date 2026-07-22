#include <iostream>
#include <sstream>
#include <string>

#include "tiny_duckdb/main/tiny_duckdb.hpp"

using tiny_duckdb::Connection;
using tiny_duckdb::TinyDuckDB;

//! A minimal REPL, duckdb-shell style. Dot commands: .tables .threads N .quit
int main() {
	TinyDuckDB db;
	Connection connection(db);
	std::cout << "tiny-duckdb shell - end statements with ';', .quit to exit\n";
	std::string buffer;
	while (true) {
		std::cout << (buffer.empty() ? "tdb> " : "  -> ");
		std::string line;
		if (!std::getline(std::cin, line)) {
			break;
		}
		if (buffer.empty() && line.rfind(".", 0) == 0) {
			std::istringstream command(line);
			std::string name;
			command >> name;
			if (name == ".quit" || name == ".exit") {
				break;
			}
			if (name == ".tables") {
				for (const auto &table : db.GetCatalog().ListTables()) {
					std::cout << table << "\n";
				}
				continue;
			}
			if (name == ".threads") {
				tiny_duckdb::idx_t threads;
				if (command >> threads) {
					db.SetThreads(threads);
				}
				std::cout << "threads: " << db.GetThreads() << "\n";
				continue;
			}
			std::cout << "unknown command: " << name << "\n";
			continue;
		}
		buffer += line;
		auto semicolon = buffer.find(';');
		if (semicolon == std::string::npos) {
			buffer += '\n';
			continue;
		}
		try {
			// the grammar does not accept the statement-terminating ';' itself
			auto result = connection.Query(buffer.substr(0, semicolon));
			std::cout << result->ToString() << "\n";
		} catch (const std::exception &ex) {
			std::cout << ex.what() << "\n";
		}
		buffer.clear();
	}
	return 0;
}
