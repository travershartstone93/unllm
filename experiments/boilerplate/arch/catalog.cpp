// Native BM25 search over complete package metadata snapshots, separately per source.
#include <xapian.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

static void index_packages(const char* destination, const char* profile_file) {
    std::map<std::string, std::string> profiles;
    std::ifstream profile_input(profile_file);
    if (!profile_input) throw std::runtime_error("Cannot read reviewed search terms");
    std::string line, name, terms;
    while (std::getline(profile_input, line)) {
        std::istringstream row(line);
        std::getline(row, name, '\t');
        std::getline(row, terms);
        profiles[name] = terms;
    }

    Xapian::WritableDatabase db(destination, Xapian::DB_CREATE);
    Xapian::TermGenerator indexer;
    indexer.set_stemmer(Xapian::Stem("english"));
    while (std::getline(std::cin, line)) {
        std::istringstream row(line);
        std::string repo, version, description;
        if (!std::getline(row, repo, '\t') || !std::getline(row, name, '\t') ||
            !std::getline(row, version, '\t')) throw std::runtime_error("Invalid catalog row");
        std::getline(row, description);
        if (repo != "aur" && repo != "core" && repo != "extra" && repo != "multilib")
            throw std::runtime_error("Unexpected catalog source");
        Xapian::Document doc;
        doc.set_data(line);
        doc.add_boolean_term(repo == "aur" ? "Saur" : "Sofficial");
        indexer.set_document(doc);
        indexer.index_text(name, 2);
        indexer.index_text(description);
        if (repo != "aur" && profiles.count(name)) indexer.index_text(profiles.at(name));
        db.add_document(doc);
    }
    if (!db.get_doccount()) throw std::runtime_error("Empty catalog");
    db.commit();
}

static void search(const char* path, const char* request) {
    Xapian::Database db(path);
    Xapian::QueryParser parser;
    parser.set_stemmer(Xapian::Stem("english"));
    parser.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
    auto query = parser.parse_query(request, 0);
    for (const char* source : {"Sofficial", "Saur"}) {
        Xapian::Enquire search(db);
        search.set_query(Xapian::Query(Xapian::Query::OP_FILTER, query, Xapian::Query(source)));
        auto hits = search.get_mset(0, 8);
        for (auto hit = hits.begin(); hit != hits.end(); ++hit)
            std::cout << hit.get_document().get_data() << '\n';
    }
}

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "index") index_packages(argv[2], argv[3]);
        else if (argc == 4 && std::string(argv[1]) == "search") search(argv[2], argv[3]);
        else throw std::runtime_error("Expected index DB PROFILES or search DB REQUEST");
    } catch (const Xapian::Error& e) {
        std::cerr << e.get_description() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
