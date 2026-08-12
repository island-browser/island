#include "search/ranker.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace island {
namespace {

using island::search::ComputeDocumentFrequency;
using island::search::CorpusStats;
using island::search::DocId;
using island::search::DocumentStat;
using island::search::FieldFrequencies;
using island::search::RankDocuments;
using island::search::RankingWeights;
using island::search::ScoredDoc;
using island::search::ScoreTerm;
using island::search::TopKDocuments;

FieldFrequencies Freq(std::initializer_list<std::pair<const std::string, std::size_t>> items) {
    return FieldFrequencies(items.begin(), items.end());
}

DocumentStat Doc(DocId id, FieldFrequencies title, FieldFrequencies url, std::size_t title_length,
                 std::size_t url_length, double age_seconds) {
    return DocumentStat{id,           std::move(title), std::move(url),
                        title_length, url_length,       age_seconds};
}

TEST(Ranker, ScoreTermIsPositiveWhenTermPresent) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat doc =
        Doc(1, Freq({{"island", 1}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/100.0);

    const double score = ScoreTerm(doc, "island", /*document_frequency=*/1, stats, {});
    EXPECT_GT(score, 0.0);
    EXPECT_TRUE(std::isfinite(score));
}

TEST(Ranker, ScoreTermIsZeroWhenTermAbsent) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat doc =
        Doc(1, Freq({{"island", 1}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/100.0);

    const double score = ScoreTerm(doc, "absent", /*document_frequency=*/1, stats, {});
    EXPECT_EQ(score, 0.0);
}

TEST(Ranker, TitleTermWeighsMoreThanUrlTerm) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat title_doc =
        Doc(1, Freq({{"island", 1}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/0.0);
    const DocumentStat url_doc =
        Doc(2, Freq({}), Freq({{"island", 1}}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/0.0);

    const double title_score = ScoreTerm(title_doc, "island", /*document_frequency=*/1, stats, {});
    const double url_score = ScoreTerm(url_doc, "island", /*document_frequency=*/1, stats, {});
    EXPECT_GT(title_score, url_score);
}

TEST(Ranker, HigherTermFrequencyScoresHigher) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat once =
        Doc(1, Freq({{"island", 1}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/0.0);
    const DocumentStat twice =
        Doc(2, Freq({{"island", 2}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/0.0);

    const double one = ScoreTerm(once, "island", /*document_frequency=*/1, stats, {});
    const double two = ScoreTerm(twice, "island", /*document_frequency=*/1, stats, {});
    EXPECT_GT(two, one);
}

TEST(Ranker, DocumentFrequencyLowersScore) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat doc =
        Doc(1, Freq({{"island", 1}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/0.0);

    const double rare = ScoreTerm(doc, "island", /*document_frequency=*/1, stats, {});
    const double common = ScoreTerm(doc, "island", /*document_frequency=*/9, stats, {});
    EXPECT_GT(rare, common);
}

TEST(Ranker, LongerDocumentScoresLowerForSameTerm) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat short_doc =
        Doc(1, Freq({{"island", 1}}), Freq({}), /*title_length=*/5, /*url_length=*/5,
            /*age_seconds=*/0.0);
    const DocumentStat long_doc =
        Doc(2, Freq({{"island", 1}}), Freq({}), /*title_length=*/50, /*url_length=*/50,
            /*age_seconds=*/0.0);

    const double short_score = ScoreTerm(short_doc, "island", /*document_frequency=*/1, stats, {});
    const double long_score = ScoreTerm(long_doc, "island", /*document_frequency=*/1, stats, {});
    EXPECT_GT(short_score, long_score);
}

TEST(Ranker, RecencyBoostIsBoundedAndZeroForOldDocs) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;

    const DocumentStat new_doc = Doc(1, Freq({}), Freq({}), /*title_length=*/5, /*url_length=*/5,
                                     /*age_seconds=*/0.0);
    const double new_score = ScoreTerm(new_doc, "absent", /*document_frequency=*/1, stats, {});
    EXPECT_EQ(new_score, 0.0);

    // RankDocuments adds the recency boost; a fresh doc ranks above an old one
    // when both otherwise score identically.
    const DocumentStat old_doc = Doc(2, Freq({}), Freq({}), /*title_length=*/5, /*url_length=*/5,
                                     /*age_seconds=*/1.0e9);
    const std::vector<DocumentStat> corpus = {old_doc, new_doc};
    RankingWeights weights;
    weights.recency_max_boost = 0.5;
    const auto ranked = RankDocuments(corpus, {"absent"}, stats, weights);
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked[0].doc_id, 1u);
    EXPECT_EQ(ranked[1].doc_id, 2u);
    EXPECT_EQ(ranked[0].score, 0.5);
    EXPECT_LT(ranked[1].score, 0.5);
}

TEST(Ranker, StopsBoostingWhenHalfLifeZero) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const DocumentStat new_doc = Doc(1, Freq({}), Freq({}), /*title_length=*/0, /*url_length=*/0,
                                     /*age_seconds=*/0.0);
    RankingWeights weights;
    weights.recency_half_life = 0.0;
    const auto ranked = RankDocuments({new_doc}, {"absent"}, stats, weights);
    ASSERT_EQ(ranked.size(), 1u);
    EXPECT_EQ(ranked[0].score, 0.0);
}

TEST(Ranker, TiesBrokenByAscendingDocId) {
    CorpusStats stats;
    stats.num_docs = 10;
    stats.avg_doc_length = 10.0;
    const std::vector<DocumentStat> corpus = {
        Doc(5, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
        Doc(2, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
        Doc(9, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
    };

    const auto ranked = RankDocuments(corpus, {"island"}, stats, {});
    ASSERT_EQ(ranked.size(), 3u);
    EXPECT_EQ(ranked[0].doc_id, 2u);
    EXPECT_EQ(ranked[1].doc_id, 5u);
    EXPECT_EQ(ranked[2].doc_id, 9u);
}

TEST(Ranker, RanksKnownOrderingByScore) {
    CorpusStats stats;
    stats.num_docs = 3;
    stats.avg_doc_length = 10.0;
    const std::vector<DocumentStat> corpus = {
        // Best: island in title.
        Doc(1, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
        // Middle: island in url only.
        Doc(2, Freq({}), Freq({{"island", 1}}), 5, 5, 0.0),
        // Worst: no island whatsoever.
        Doc(3, Freq({}), Freq({}), 5, 5, 0.0),
    };

    const auto ranked = RankDocuments(corpus, {"island"}, stats, {});
    ASSERT_EQ(ranked.size(), 3u);
    EXPECT_EQ(ranked[0].doc_id, 1u);
    EXPECT_EQ(ranked[1].doc_id, 2u);
    EXPECT_EQ(ranked[2].doc_id, 3u);
}

TEST(Ranker, ZeroKReturnsEmpty) {
    CorpusStats stats;
    stats.num_docs = 3;
    stats.avg_doc_length = 10.0;
    const std::vector<DocumentStat> corpus = {
        Doc(1, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
        Doc(2, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
    };
    EXPECT_TRUE(TopKDocuments(corpus, {"island"}, stats, {}, /*k=*/0).empty());
}

TEST(Ranker, KGreaterThanCorpusReturnsFullRanking) {
    CorpusStats stats;
    stats.num_docs = 2;
    stats.avg_doc_length = 10.0;
    const std::vector<DocumentStat> corpus = {
        Doc(1, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
        Doc(2, Freq({}), Freq({{"island", 1}}), 5, 5, 0.0),
    };
    const auto top = TopKDocuments(corpus, {"island"}, stats, {}, /*k=*/10);
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0].doc_id, 1u);
    EXPECT_EQ(top[1].doc_id, 2u);
}

TEST(Ranker, TopKReturnsBestInOrderForSmallK) {
    CorpusStats stats;
    stats.num_docs = 5;
    stats.avg_doc_length = 10.0;
    const std::vector<DocumentStat> corpus = {
        Doc(1, Freq({{"island", 1}}), Freq({}), 5, 5, 1.0e9),  // old
        Doc(2, Freq({}), Freq({}), 5, 5, 0.0),                 // no term
        Doc(3, Freq({{"island", 2}}), Freq({}), 5, 5, 0.0),    // best
        Doc(4, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),    // fresh single
        Doc(5, Freq({}), Freq({{"island", 1}}), 5, 5, 0.0),    // url only
    };

    const auto top = TopKDocuments(corpus, {"island"}, stats, {}, /*k=*/2);
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0].doc_id, 3u);
    EXPECT_EQ(top[1].doc_id, 4u);
}

TEST(Ranker, TopKWithZeroStatsProducesDeterministicLogicalOrder) {
    CorpusStats stats;  // num_docs == 0, avg_doc_length == 0.
    const std::vector<DocumentStat> corpus = {
        Doc(1, Freq({{"island", 1}}), Freq({}), 0, 0, 0.0),
        Doc(2, Freq({{"island", 1}}), Freq({}), 0, 0, 0.0),
    };

    const auto ranked = RankDocuments(corpus, {"island"}, stats, {});
    ASSERT_EQ(ranked.size(), 2u);
    for (const ScoredDoc& sd : ranked) {
        EXPECT_TRUE(std::isfinite(sd.score));
        EXPECT_GE(sd.score, 0.0);
    }
    EXPECT_EQ(ranked[0].doc_id, 1u);
    EXPECT_EQ(ranked[1].doc_id, 2u);
}

TEST(Ranker, ScoresNeverNaNOrInfAcrossAdversarialStats) {
    RankingWeights weights;
    weights.k1 = -1.0;
    weights.b = -0.5;
    weights.title_weight = -2.0;
    weights.url_weight = -3.0;
    weights.recency_max_boost = -0.4;
    weights.recency_half_life = 0.0;

    CorpusStats stats;
    stats.num_docs = 0;
    stats.avg_doc_length = 0.0;

    const DocumentStat doc = Doc(1, Freq({{"island", 1}}), Freq({{"island", 1}}), 0, 0, 1.0e9);
    const auto ranked = RankDocuments({doc}, {"island"}, stats, weights);
    ASSERT_EQ(ranked.size(), 1u);
    EXPECT_TRUE(std::isfinite(ranked[0].score));
    EXPECT_GE(ranked[0].score, 0.0);
}

TEST(Ranker, ComputeDocumentFrequencyCountsDocOnceAcrossFields) {
    const std::vector<DocumentStat> corpus = {
        Doc(1, Freq({{"island", 1}}), Freq({{"island", 1}}), 5, 5, 0.0),
        Doc(2, Freq({}), Freq({{"island", 1}}), 5, 5, 0.0),
        Doc(3, Freq({}), Freq({}), 5, 5, 0.0),
    };
    EXPECT_EQ(ComputeDocumentFrequency(corpus, "island"), 2u);
}

TEST(Ranker, RankDocumentsIsStableAcrossEqualScores) {
    CorpusStats stats;
    stats.num_docs = 2;
    stats.avg_doc_length = 10.0;
    const std::vector<DocumentStat> corpus = {
        Doc(7, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
        Doc(3, Freq({{"island", 1}}), Freq({}), 5, 5, 0.0),
    };
    const auto ranked = RankDocuments(corpus, {"island"}, stats, {});
    for (const ScoredDoc& sd : ranked) {
        EXPECT_TRUE(std::isfinite(sd.score));
    }
    EXPECT_EQ(ranked[0].doc_id, 3u);
    EXPECT_EQ(ranked[1].doc_id, 7u);
}

}  // namespace
}  // namespace island