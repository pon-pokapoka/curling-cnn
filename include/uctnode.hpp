#ifndef UCTNODE_HPP
#define UCTNODE_HPP

#include <torch/script.h>
#include <torch/cuda.h>
#include <vector>

#include "config.hpp"
#include "digitalcurling3/digitalcurling3.hpp"

namespace dc = digitalcurling3;

using namespace config;

class UctNode
{
    public:
        typedef std::unique_ptr<UctNode> Ptr;

    public:
        UctNode();
        void    CreateChild(std::pair<int, int>);
        // void    expandChild(int);
        // void    resetAsRoot();
        // void    removeChild(Ptr);

        UctNode* GetChild(std::pair<int, int>);
        UctNode* GetParent();

        void SetGameState(dc::GameState);
        dc::GameState GetGameState();
        void SetPolicy(torch::Tensor);
        void SetFilter(std::array<std::array<std::array<bool, policy_width>, policy_weight>, policy_rotation>);
        // void SetValue(float);

        torch::Tensor GetFilter();

        void SetEvaluatedResults(torch::Tensor, float);

        void SetSimulated();
        void SetEvaluated();

        bool GetEvaluated();

        torch::Tensor GetPolicy();
        float GetValue();

        std::array<std::array<UctNode*, nSimulation>, policy_weight*policy_width*policy_rotation> GetChildNodeAddresses();

        void SetValue(float);
        void SetCount(int);
        float GetCountValue();

        int GetVisitCount();

        void SetChildCountValue(int, int, float);

        torch::Tensor GetChildVisitCount();
        torch::Tensor GetChildSumValue();

        std::pair<int, int> GetIndices();

    private:
        UctNode* parent_;
        dc::GameState       game_state_;
        std::pair<int, int> indices_;

        torch::Tensor       policy_;
        torch::Tensor       filter_;
        float   value_;

        int     visit_count_;
        float   sum_value_;

        std::vector<Ptr>    child_nodes_;
        std::array<std::array<UctNode*, nSimulation>, policy_weight*policy_width*policy_rotation>    child_node_addresses_;

        torch::Tensor       child_visit_count_;
        torch::Tensor       child_sum_value_;

        bool    simulated_;
        bool    evaluated_;

};

#endif // UCTNODE_HPP