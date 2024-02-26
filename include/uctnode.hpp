#ifndef UCTNODE_HPP
#define UCTNODE_HPP

#include <torch/script.h>
#include <torch/cuda.h>
#include <vector>

#include "digitalcurling3/digitalcurling3.hpp"

namespace dc = digitalcurling3;

class UctNode
{
    public:
        UctNode();
        void    CreateChild(int);
        // void    expandChild();
        void    resetAsRoot();
        void    removeChild(UctNode* child);

        UctNode* GetChild(int);

        void SetGameState(dc::GameState);
        dc::GameState GetGameState();
        void SetPolicy(torch::Tensor);
        void SetFilter(torch::Tensor);
        void SetValue(float);

        void SetEvaluatedResults(torch::Tensor, float);

        void SetSimulated();
        void SetEvaluated();

        bool GetEvaluated();

        float GetValue();

        std::vector<UctNode*> GetChildNodes();
        std::vector<int> GetChildIndices();

    private:
        UctNode* parent_;
        dc::GameState       game_state_;

        torch::Tensor    policy_;
        torch::Tensor    filter_;
        float   value_;

        int visit_count_;
        float sum_value_;

        std::vector<UctNode*>    child_nodes_;
        // std::vector<int*>    child_visit_count_;
        // std::vector<float*>    child_sum_value_;
        std::vector<int>    child_move_indices_;

        bool    simulated_;
        bool    evaluated_;

}

#endif // UCTNODE_HPP