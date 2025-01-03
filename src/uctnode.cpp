#include "uctnode.hpp"

UctNode::UctNode():
parent_(nullptr),
game_state_(),
child_nodes_(),
child_move_indices_(),
evaluated_(false),
simulated_(false),
value_(-1),
visit_count_(0),
sum_value_(0),
child_visit_count_(torch::zeros({1, policy_weight*policy_width*policy_rotation}, torch::kInt)),
child_sum_value_(torch::zeros({1, policy_weight*policy_width*policy_rotation}, torch::kFloat))
{}

void UctNode::CreateChild(int index) {
    std::unique_ptr<UctNode> child(new UctNode());
    child->parent_ = this;
    child_nodes_.push_back(std::move(child));

    child_move_indices_.push_back(index);
}

// void UctNode::expandChild(int childData) {
//     std::unique_ptr<UctNode> newChild = new UctNode(childData);
//     addChild(newChild);
// }

// void UctNode::resetAsRoot() {
    // if (parent_ != nullptr) {
    //     parent_->removeChild(this);
    //     parent_ = nullptr;
    // }
// }

// void UctNode::removeChild(std::unique_ptr<UctNode> child) {
//     auto it = std::find(child_nodes_.begin(), child_nodes_.end(), child);
//     if (it != child_nodes_.end()) {
//         child_nodes_.erase(it);
//     }
// }

UctNode* UctNode::GetChild(int index)
{
    auto it = std::find(child_move_indices_.begin(), child_move_indices_.end(), index);

    return child_nodes_[it - child_move_indices_.begin()].get();
}

UctNode* UctNode::GetChildById(int index)
{
    return child_nodes_[index].get();
}


UctNode* UctNode::GetParent()
{
    return parent_;
}

void UctNode::SetGameState(dc::GameState game_state)
{
    game_state_ = game_state;
    simulated_ = true;
}

dc::GameState UctNode::GetGameState()
{
    return game_state_;
}

void UctNode::SetPolicy(torch::Tensor policy)
{
    policy_ = policy;
}

void UctNode::SetFilter(std::array<std::array<std::array<bool, policy_width>, policy_weight>, policy_rotation> filter)
{
    filter_ = torch::from_blob(filter.data(), {policy_rotation, policy_weight, policy_width}, torch::kInt8);
}

// void UctNode::SetValue(float value)
// {
//     value_ = value;
// }

torch::Tensor UctNode::GetFilter()
{
    return filter_;
}

void UctNode::SetEvaluatedResults(torch::Tensor policy, float value)
{
    policy_ = policy;
    value_ = value;
    evaluated_ = true;
}

void UctNode::SetSimulated()
{
    simulated_ = true;
}

void UctNode::SetEvaluated()
{
    evaluated_ = true;
}

bool UctNode::GetEvaluated()
{
    return evaluated_;
}

torch::Tensor UctNode::GetPolicy()
{
    return policy_;
}

float UctNode::GetValue()
{
    return value_;
}

std::vector<UctNode*> UctNode::GetChildNodes()
{
    std::vector<UctNode*> raw_vector;
    for (const auto& node : child_nodes_) {
        raw_vector.push_back(node.get());
    }
    return raw_vector;
}

std::vector<int> UctNode::GetChildIndices()
{
    return child_move_indices_;
}

void UctNode::SetValue(float value)
{
    sum_value_ += value;
}

void UctNode::SetCount(int count)
{
    visit_count_ += count;
}

float UctNode::GetCountValue()
{
    return sum_value_ / visit_count_;
}

int UctNode::GetVisitCount()
{
    return visit_count_;
}

void UctNode::SetChildCountValue(int index, int count, float value)
{
    child_visit_count_.index({0, index}) += count;
    child_sum_value_.index({0, index}) += value;
}

torch::Tensor UctNode::GetChildVisitCount()
{
    return child_visit_count_;
}

torch::Tensor UctNode::GetChildSumValue()
{
    return child_sum_value_;
}