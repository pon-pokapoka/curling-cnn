#ifndef UCTNODE_HPP
#define UCTNODE_HPP

#include <torch/script.h>
#include <torch/cuda.h>
#include <vector>

#include "digitalcurling3/digitalcurling3.hpp"

namespace dc = digitalcurling3;

int const policy_weight = 16;
int const policy_width = 32;
int const policy_rotation = 2;


class UctNode
{
    public:
        typedef std::unique_ptr<UctNode> Ptr;

    public:
        UctNode();
        void    CreateChild(int);
        // void    expandChild(int);
        // void    resetAsRoot();
        // void    removeChild(Ptr);

        UctNode* GetChild(int);
        UctNode* GetChildById(int);
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

        std::vector<UctNode*> GetChildNodes();
        std::vector<int> GetChildIndices();

        void SetValue(float);
        void SetCount(int);
        float GetCountValue();

        int GetVisitCount();

        void SetChildCountValue(int, int, float);

        torch::Tensor GetChildVisitCount();
        torch::Tensor GetChildSumValue();

    private:
        UctNode* parent_;
        dc::GameState       game_state_;

        torch::Tensor       policy_;
        torch::Tensor       filter_;
        float   value_;

        int     visit_count_;
        float   sum_value_;

        std::vector<Ptr>    child_nodes_;
        std::vector<int>    child_move_indices_;

        torch::Tensor       child_visit_count_;
        torch::Tensor       child_sum_value_;

        bool    simulated_;
        bool    evaluated_;

};

#endif // UCTNODE_HPP