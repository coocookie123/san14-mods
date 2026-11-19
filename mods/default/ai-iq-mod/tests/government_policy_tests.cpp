#include "../src/government_policy.h"
#include "government_score_golden.h"
#include <cassert>
#include <iostream>

namespace policy = companion_government_policy;

int main() {
    const auto contexts = golden_contexts();
    for (size_t context = 0; context < contexts.size(); ++context) {
        for (int id = 1; id <= 120; ++id) {
            San14GovernmentCandidate candidate{sizeof(candidate), 100, id, id % 10 + 1, id % 10 + 1, static_cast<uint32_t>(id % 2)};
            const int score = policy::score_policy(id, candidate.effective_level, contexts[context], candidate);
            if (score != government_golden[context][id - 1]) {
                std::cerr << "policy mismatch " << context << " " << id << " " << score << "\n";
                return 1;
            }
        }
    }
    assert(policy::hex_distance(-1, 0) == 9999 && policy::hex_distance(1000, 1000) == 0);
    for (int i = 0; i < 5; ++i) assert(policy::department(46 + i) == i);
    std::cout << "companion government policy: 960 JS score comparisons passed\n";
    return 0;
}
