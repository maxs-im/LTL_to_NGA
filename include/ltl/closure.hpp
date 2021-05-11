#pragma once

#include "ltl/ltl.hpp"

#include <vector>
#include <set>
#include <algorithm>
#include <tuple>

// TODO: remove iostream
#include <iostream>

namespace ltl
{

class converting
{
public:
    using state_t = std::vector<ltl::node_t>;
    using indexes_container_t = std::vector<size_t>;
    // index of At -> first -- alph, second -- next states indexes of At
    using table_t = std::vector<std::pair<std::set<uint32_t>, indexes_container_t>>;

    static std::shared_ptr<converting> construct(ltl::node_t formula)
    {
        return std::shared_ptr<converting>(new converting(std::move(formula)));
    }

    const state_t& get_concrete_state(const size_t index)
    {
        return m_atomic_plurality[index];
    }

    /// \return A, f, A_0, F
    std::tuple<indexes_container_t, table_t, indexes_container_t, std::vector<indexes_container_t>> ltl_to_nga()
    {
        const std::vector<state_t> &At = m_atomic_plurality;
        indexes_container_t A{};
        std::vector<indexes_container_t> F{m_until_count};
        table_t f{At.size()};

        // initial state calculation
        indexes_container_t A_0_indexes{};
        for (size_t i = 0; i < At.size(); ++i)
        {
            const state_t &s = At[i];
            if (std::any_of(s.begin(), s.end(), [this](const ltl::node_t& it) { return it == m_formula; }))
                A_0_indexes.push_back(i);
        }

        indexes_container_t C_indexes = A_0_indexes;
        while (!C_indexes.empty())
        {
            const size_t s_index = C_indexes.back();
            const state_t &s = At[s_index];
            C_indexes.pop_back();

            A.emplace_back(s_index);

            int i = 0;
            for (const ltl::node_t &alpha : m_closure_storage)
            {
                /// rule Z1
                if (alpha->get_kind() == ltl::kind::until)
                {
                    const bool is_alpha_in_s = std::any_of(s.begin(), s.end(),
                                     [&alpha](const ltl::node_t &it) { return it == alpha; });
                    const bool is_q_in_s = std::any_of(s.begin(), s.end(),
                                    [right = std::dynamic_pointer_cast<ltl_until>(alpha)->m_right]
                                    (const ltl::node_t &it) { return it == right; });
                    if (!is_alpha_in_s || is_q_in_s)
                        F[i].emplace_back(s_index);
                    // till next until
                    ++i;
                }
            }

            indexes_container_t next_states_indexes{};
            for (size_t sd_index = 0; sd_index < At.size(); ++sd_index)
            {
                /// rule R1-R2
                if (is_satisfies_r_rules(s, At[sd_index]))
                {
                    if (!std::any_of(A.begin(), A.end(),
                                     [&sd_index](const size_t &it) { return it == sd_index; }))
                        C_indexes.push_back(sd_index);
                }
            }

            if (!next_states_indexes.empty())
            {
                // collect all atomic propositions that become curves
                std::set<uint32_t> alph_proposition{};
                for (const auto &node : s)
                    if (node->get_kind() == ltl::kind::atom)
                        alph_proposition.insert(std::dynamic_pointer_cast<ltl_atom>(node)->m_index);

                assert(f[s_index].second.empty() && "Should be empty according to the algorithm");
                f[s_index] = std::make_pair(std::move(alph_proposition), std::move(next_states_indexes));
            }

        }

        return std::make_tuple(A, f, A_0_indexes, F);
    }

private:
    explicit converting(ltl::node_t&& formula) : m_formula(std::move(formula))
    {
        fill_closure();
        generate_atomic_plurality();
    }

    /// rules R1-R2 + modified with neg
    static bool is_satisfies_r_rules(const state_t &s, const state_t &sd)
    {
        for (const auto &node_s : s)
        {
            switch (node_s->get_kind())
            {
                /// rule R1
                case ltl::kind::next:
                {
                    const auto &a = std::dynamic_pointer_cast<ltl_next>(node_s)->m_xformula;
                    if (!std::any_of(sd.begin(), sd.end(), [&a](const auto &node_sd)
                                    { return node_sd == a; }))
                        return false;
                    break;
                }
                /// rule R2
                case ltl::kind::until:
                {
                    const auto node_s_until = std::dynamic_pointer_cast<ltl_until>(node_s);
                    bool is_b_in_s = std::any_of(s.begin(), s.end(), [&b = node_s_until->m_right](const auto &nd_s)
                                                { return nd_s == b; });
                    bool is_a_in_s = std::any_of(s.begin(), s.end(), [&a = node_s_until->m_left](const auto &nd_s)
                                                { return nd_s == a; });
                    bool is_node_s_in_sd = std::any_of(sd.begin(), sd.end(), [&node_s](const auto &node_sd)
                                                { return node_sd == node_s; });

                    if (is_b_in_s || (is_a_in_s && is_node_s_in_sd))
                        continue;

                    return false;
                }
                case ltl::kind::negation:
                {
                    const auto &neg_node_s = std::dynamic_pointer_cast<ltl_negation>(node_s)->m_formula;
                    switch (neg_node_s->get_kind())
                    {
                        /// rule R1 neg (mine)
                        case ltl::kind::next:
                        {
                            auto a = std::dynamic_pointer_cast<ltl_next>(neg_node_s)->m_xformula;
                            const auto neg_a = ltl_negation::construct(std::move(a));
                            if (std::any_of(sd.begin(), sd.end(), [&neg_a](const auto &node_sd)
                                            { return node_sd == neg_a; }))
                                continue;
                            return false;
                        }
                        /// WIP: rule R2 neg (mine)
                        case ltl::kind::until:
                        {
                            // TODO: consult!!!
                            std::cout << "WARNING: need to consult (negative until for the R2 rule)\n";
                            break;
                        }
                        case ltl::kind::negation:
                            assert("Shouldn't happen");
                            break;
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        return true;
    }

    void generate_atomic_plurality()
    {
        state_t neg_storage{m_closure_storage};
        for (ltl::node_t &it : neg_storage)
            it = ltl_negation::construct(std::move(it));

        recursive_brute_force(m_atomic_plurality, m_closure_storage, neg_storage);
    }

    // TODO: optimize!!! (depth 2^ln(formula))
    static void recursive_brute_force(std::vector<state_t> &states, const state_t &pos, const state_t &neg,
                               int ip = -1, int in = -1, state_t curr = {})
    {
        if (ip >= pos.size() && in >= neg.size())
        {
            if (is_correct_atomic(curr))
                states.emplace_back(curr);
            return;
        }

        if (ip < pos.size())
        {
            curr.emplace_back(pos[ip]);
            recursive_brute_force(states, pos, neg, ip+1, in, curr);
            curr.pop_back();
        }

        if (in < neg.size()){
            curr.emplace_back(neg[in]);
            recursive_brute_force(states, pos, neg, ip, in+1, curr);
            curr.pop_back();
        }
    }

    static bool is_correct_atomic(const state_t &atomic)
    {
        const auto find_fn = [&atomic](const ltl::node_t& node) -> bool
        {
            return std::any_of(atomic.begin(), atomic.end(),
                               [&node](const ltl::node_t& it) { return it == node; });
        };

        for (auto f : atomic)
        {
            switch (f->get_kind())
            {
                case ltl::kind::one:
                case ltl::kind::atom:
                case ltl::kind::next:
                    break;
                case ltl::kind::negation:
                {
                    const auto &pos = std::dynamic_pointer_cast<ltl_negation>(f)->m_formula;
                    switch (pos->get_kind())
                    {
                        case ltl::kind::one:
                        case ltl::kind::atom:
                        case ltl::kind::next:
                            break;
                        case ltl::kind::negation:
                            assert("Shouldn't happen");
                            break;
                        case ltl::kind::conjunction:
                        {
                            auto left = std::dynamic_pointer_cast<ltl_conjunction>(f)->m_left;
                            auto right = std::dynamic_pointer_cast<ltl_conjunction>(f)->m_right;
                            const auto neg_left = ltl_negation::construct(std::move(left));
                            const auto neg_right = ltl_negation::construct(std::move(right));

                            /// rule 5: !(a ^ b) = !a ∨ !b
                            if (find_fn(neg_left) || find_fn(neg_right))
                                continue;

                            return false;
                        }
                        case ltl::kind::until:
                        {
                            auto left = std::dynamic_pointer_cast<ltl_until>(f)->m_left;
                            auto right = std::dynamic_pointer_cast<ltl_until>(f)->m_right;
                            auto neg_left = ltl_negation::construct(std::move(left));
                            auto neg_right = ltl_negation::construct(std::move(right));

                            // calculate before exit
                            const bool is_neg_right_in = find_fn(neg_right);

                            const auto conjunction = ltl_conjunction::construct(std::move(neg_right),
                                                                                std::move(neg_left));

                            // TODO: consult!!!
                            /// rule MINE: !(a U b) = !a R !b = al
                            // if al in atomic then !a^!b OR !b should be in atomic
                            if (is_neg_right_in || find_fn(conjunction))
                                continue;

                            return false;
                        }
                        default:
                            break;
                    }
                    break;
                }
                case ltl::kind::conjunction:
                    /// rule 3-4
                    if (find_fn(std::dynamic_pointer_cast<ltl_conjunction>(f)->m_left) &&
                          find_fn(std::dynamic_pointer_cast<ltl_conjunction>(f)->m_right))
                        continue;

                    return false;
                case ltl::kind::until:
                    /// rule 2
                    if (find_fn(std::dynamic_pointer_cast<ltl_until>(f)->m_left) ||
                          find_fn(std::dynamic_pointer_cast<ltl_until>(f)->m_right))
                        continue;

                    return false;
                default:
                    break;
            }

            /// rule 1 : that is a redundant check, but let it be
            auto neg = ltl_negation::construct(std::move(f));
            if (find_fn(neg))
            {
                assert("Shouldn't happen by the idea of implementing the code");
                return false;
            }
        }

        return true;
    }

    void fill_closure()
    {
        const auto& node = m_formula->get_kind() == ltl::kind::negation ?
                            std::dynamic_pointer_cast<ltl_negation>(m_formula)->m_formula :
                            m_formula;

        for (const auto &it : m_closure_storage)
            if (it == node)
                return;

        // AP saving
        if (node->get_kind() == ltl::kind::atom)
        {
            [[maybe_unused]] auto [_, suc] = p_indexes.insert(std::dynamic_pointer_cast<ltl_atom>(node)->m_index);
            assert(suc && "Duplicate shouldn't appear by code idea");
        }

        // until counter that will be helpful during algorithm
        if (node->get_kind() == ltl::kind::until)
            ++m_until_count;

        m_closure_storage.emplace_back(node);
    }

    const ltl::node_t m_formula{nullptr};
    std::set<uint32_t> p_indexes{};
    size_t m_until_count{0};
    std::vector<ltl::node_t> m_closure_storage{};
    std::vector<std::vector<ltl::node_t>> m_atomic_plurality{};
};

} // namespace ltl
