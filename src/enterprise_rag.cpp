// Copyright (c) 2026 HotKV Ltd
// Licensed under the Apache License, Version 2.0. See LICENSE in the project root.

#include <hotkv/enterprise.hpp>

#include "pairs_util.hpp"

namespace hotkv {

void RagCommands::create(std::string_view name, int64_t dimensions, const RagCreateOptions& opts) {
    std::vector<std::string> args{"RAG.CREATE", std::string(name), std::to_string(dimensions)};
    if (!opts.strategy.empty()) {
        args.emplace_back("STRATEGY");
        args.push_back(opts.strategy);
    }
    if (opts.chunk_size.has_value()) {
        args.emplace_back("CHUNK_SIZE");
        args.push_back(std::to_string(*opts.chunk_size));
    }
    if (opts.chunk_overlap.has_value()) {
        args.emplace_back("CHUNK_OVERLAP");
        args.push_back(std::to_string(*opts.chunk_overlap));
    }
    client_.command(std::move(args));
}

int64_t RagCommands::ingest(std::string_view pipeline, std::string_view doc_id, std::string_view text,
                             const RagIngestOptions& opts) {
    std::vector<std::string> args{"RAG.INGEST", std::string(pipeline), std::string(doc_id), std::string(text)};
    if (!opts.meta.empty()) {
        args.emplace_back("META");
        for (const auto& [k, v] : opts.meta) {
            args.push_back(k);
            args.push_back(v);
        }
    }
    if (!opts.vectors_csv.empty()) {
        args.emplace_back("VECTORS");
        args.insert(args.end(), opts.vectors_csv.begin(), opts.vectors_csv.end());
    }
    return client_.command(std::move(args)).as_int();
}

std::vector<RagSearchResult> RagCommands::search(std::string_view pipeline, std::string_view query_text,
                                                   const RagSearchOptions& opts) {
    std::vector<std::string> args{"RAG.SEARCH", std::string(pipeline), std::string(query_text)};
    if (opts.k.has_value()) {
        args.emplace_back("K");
        args.push_back(std::to_string(*opts.k));
    }
    if (opts.alpha.has_value()) {
        args.emplace_back("ALPHA");
        args.push_back(detail::format_finite(*opts.alpha));
    }
    if (opts.vector.has_value()) {
        args.emplace_back("VECTOR");
        std::string csv;
        for (std::size_t i = 0; i < opts.vector->size(); ++i) {
            if (i != 0) {
                csv += ',';
            }
            csv += detail::format_finite((*opts.vector)[i]);
        }
        args.push_back(csv);
    }
    // FILTER is consumed by the server until K/ALPHA/VECTOR or end of
    // args, so it must be emitted last.
    if (!opts.filter.empty()) {
        args.emplace_back("FILTER");
        for (const auto& [k, v] : opts.filter) {
            args.push_back(k);
            args.push_back(v);
        }
    }

    Reply r = client_.command(std::move(args));
    std::vector<RagSearchResult> out;
    for (const Reply& item : r.as_array()) {
        const Reply::Array& fields = item.as_array();
        RagSearchResult res;
        res.chunk_id = fields.at(0).as_string();
        res.doc_id = fields.at(1).as_string();
        res.text = fields.at(2).as_string();
        res.score = fields.at(3).as_double();
        res.vector_score = fields.at(4).as_double();
        res.bm25_score = fields.at(5).as_double();
        for (std::size_t i = 6; i + 1 < fields.size(); i += 2) {
            res.metadata.emplace_back(fields[i].as_string(), fields[i + 1].as_string());
        }
        out.push_back(std::move(res));
    }
    return out;
}

RagStats RagCommands::stats(std::string_view pipeline) {
    auto pairs = client_.command({"RAG.STATS", std::string(pipeline)}).as_pairs();
    RagStats s;
    s.name = detail::pairs_string(pairs, "name");
    s.dimensions = detail::pairs_int(pairs, "dimensions");
    s.total_docs = detail::pairs_int(pairs, "totalDocs");
    s.total_chunks = detail::pairs_int(pairs, "totalChunks");
    s.total_ingested = detail::pairs_int(pairs, "totalIngested");
    s.total_queries = detail::pairs_int(pairs, "totalQueries");
    s.chunk_size = detail::pairs_int(pairs, "chunkSize");
    s.chunk_strategy = detail::pairs_string(pairs, "chunkStrategy");
    s.chunk_overlap = detail::pairs_int(pairs, "chunkOverlap");
    return s;
}

void RagCommands::drop(std::string_view pipeline) {
    client_.command({"RAG.DROP", std::string(pipeline)});
}

std::vector<std::string> RagCommands::list() {
    return client_.command({"RAG.LIST"}).as_string_array();
}

} // namespace hotkv
