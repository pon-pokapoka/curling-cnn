#include "skip.hpp"

#include <iostream>
#include <omp.h>

#include <torch/script.h>
#include <torch/cuda.h>
#include <torch/csrc/api/include/torch/nn/functional/activation.h>
#include <c10/cuda/CUDACachingAllocator.h>

#include "utility.hpp"
#include "readcsv.hpp"
#include "executablepath.hpp"

namespace F = torch::nn::functional;

// int const policy_weight = 16;
// int const policy_width = 32;
// int const policy_rotation = 2;

Skip::Skip() : module(),
g_game_setting(),
g_simulators(),
g_players(),
limit(),
win_table(),
device(torch::kCPU),
dtype(torch::kBFloat16),
queue_evaluate(),
queue_simulate(),
queue_create_child(),
queue_create_child_index(),
flag_create_child(),
temp_game_states()
{
    #pragma omp parallel for
    for (auto i=0; i < nLoop; ++i) {
        flag_create_child[i] = false;
    }
}


void Skip::OnInit(dc::Team const g_team, dc::GameSetting const& game_setting, std::unique_ptr<dc::ISimulatorFactory> simulator_factory,     std::array<std::unique_ptr<dc::IPlayerFactory>, 4> player_factories,  std::array<size_t, 4> & player_order)
{
    team = g_team;

    // Retrieve and display the executable's absolute path
    std::filesystem::path dcRootPath;

    std::string execPath = getExecutablePath();
    if (execPath.empty()) {
        std::cerr << "Failed to retrieve executable path." << std::endl;
        dcRootPath = "../";
    }

    std::cout << "Executable Path: " << execPath << std::endl;

    // If you want to work with the path, use std::filesystem
    std::filesystem::path execFilePath(execPath);
    std::cout << "Executable Directory: " << execFilePath.parent_path() << std::endl;

    dcRootPath = execFilePath.parent_path().parent_path();

    win_table = readcsv(dcRootPath / "model" / "win_table.csv");

    // for (const auto& row : win_table) {
    //     for (const auto& value : row) {
    //         std::cout << value << ' ';
    //     }
    //     std::cout << std::endl;
    // }

    torch::NoGradGuard no_grad; 

    device = torch::kCPU;
    if (torch::cuda::is_available()) {
        std::cout << "CUDA is available!" << std::endl;
        device = torch::kCUDA;
    }   
    else {
        std::cout << "CUDA is not available." << std::endl;
    }
    // Deserialize the ScriptModule from a file using torch::jit::load().
    try {
        // Deserialize the ScriptModule from a file using torch::jit::load().
        std::cout << "model loading..." << std::endl;
        module = torch::jit::load(dcRootPath / "model" / "traced_fixsize_lighttest+shotbyshot_1-010.pt", device);
        module.to(dtype);
        std::cout << "model loaded" << std::endl;
    }
    catch (const c10::Error& e) {
        std::cerr << "error loading the model\n";
    }

    // ここでCNNによる推論を行うことで、次回以降の速度が早くなる
    // 使うバッチサイズすべてで行っておく
    std::cout << "initial inference\n";
    for (auto i = 0; i < 10; ++i) {
        std::cout << "." << std::flush;
        std::vector<torch::jit::IValue> inputs;
        inputs.push_back(torch::rand({nBatchSize, 18, 32, 16}, dtype).to(device));

        // Execute the model and turn its output into a tensor.
        auto outputs = module.forward(inputs).toTuple();
        torch::Tensor out1 = outputs->elements()[0].toTensor().reshape({nBatchSize, policy_weight * policy_width * policy_rotation}).to(torch::kCPU);
    }
    std::cout << std::endl;
    for (auto i = 0; i < 10; ++i) {
        std::cout << "." << std::flush;
        std::vector<torch::jit::IValue> inputs;
        inputs.push_back(torch::rand({1, 18, 32, 16}, dtype).to(device));

        // Execute the model and turn its output into a tensor.
        auto outputs = module.forward(inputs).toTuple();
        torch::Tensor out1 = outputs->elements()[0].toTensor().reshape({1, policy_weight * policy_width * policy_rotation}).to(torch::kCPU);
    }
    c10::cuda::CUDACachingAllocator::emptyCache();
    std::cout << std::endl;

    // シミュレータFCV1Lightを使用する．
    g_game_setting = game_setting;
    for (unsigned i = 0; i < nLoop; ++i) {
        g_simulators[i] = dc::simulators::SimulatorFCV1LightFactory().CreateSimulator();
    }
    g_simulator_storage = g_simulators[0]->CreateStorage();

    // プレイヤーを生成する
    // 非対応の場合は NormalDistプレイヤーを使用する．
    assert(g_players.size() == player_factories.size());
    for (size_t i = 0; i < g_players.size(); ++i) {
        auto const& player_factory = player_factories[player_order[i]];
        if (player_factory) {
            g_players[i] = player_factory->CreatePlayer();
        } else {
            g_players[i] = dc::players::PlayerNormalDistFactory().CreatePlayer();
        }
    }

    dc::GameState temp_game_state(g_game_setting);
    kShotPerEnd = static_cast<int>(temp_game_state.kShotPerEnd);

    // 考慮時間制限
    // ショット数で等分するが、超過分を考慮して0.8倍しておく
    limit = g_game_setting.thinking_time[0] * 0.8 / (kShotPerEnd/2) / g_game_setting.max_end;

    // ショットシミュレーションの動作確認
    // しなくて良い
    std::cout << "initial simulation\n";
    for (auto j = 0; j < 10; ++j) {
        std::cout << "." << std::flush;
        dc::GameState dummy_game_state(g_game_setting);
        std::array<dc::GameState, nBatchSize> dummy_game_states; 
        std::array<dc::Move, nBatchSize> dummy_moves;
        auto & dummy_player = *g_players[0];
        dc::moves::Shot dummy_shot = {dc::Vector2(0, 2.5), dc::moves::Shot::Rotation::kCW};
        #pragma omp parallel for
        for (auto i=0; i < nBatchSize; ++i) {
            dummy_game_states[i] = dummy_game_state;
        }
        #pragma omp parallel for
        for (auto i=0; i < nBatchSize; ++i) {
            dummy_moves[i] = dummy_shot;
            g_simulators[i]->Load(*g_simulator_storage);

            dc::ApplyMove(g_game_setting, *g_simulators[i],
                dummy_player, dummy_game_states[i], dummy_moves[i], std::chrono::milliseconds(0));
        }
    }
    std::cout << std::endl;

    filt = torch::from_blob(utility::createFilter().data(), {policy_rotation, policy_weight, policy_width}, torch::kBool);

    // std::cout << filt << std::endl;

    // std::cout << utility::createFilter() << std::endl;

    // for (auto i=0; i < policy_rotation; ++i){
    //     for (auto j=0; j < policy_weight; ++j) {
    //         for (auto k=0; k < policy_width; ++k) {
    //             std::cout << filt.index({i, j, k}).item<bool>();
    //         }
    //         std::cout << "\n";
    //     }
    //     std::cout << "\n\n";
    // }


    // for (auto i=0; i < policy_rotation; ++i){
    //     for (auto j=0; j < policy_weight; ++j) {
    //         for (auto k=0; k < policy_width; ++k) {
    //             std::cout << utility::createFilter()[utility::Id3d1d(i, j, k)];
    //         }
    //         std::cout << "\n";
    //     }
    //     std::cout << "\n\n";
    // }
}

float Skip::search(UctNode* current_node, int k)
{
    float result = current_node->GetValue();
    // output = evaluate(current_node);
    // set_policy_value(current_node, output);
    // set_filter(current_node)

    // current_node.getPolicy();
    // filt = current_node.getFilter();

    if (!((current_node->GetGameState().shot == 0) && (current_node->GetParent()))) {        
        torch::Tensor policy;
        std::random_device rd;  // Will be used to obtain a seed for the random number engine
        std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
        std::uniform_real_distribution<> dis(0.0, 1.0);

        policy = current_node->GetPolicy();

        // std::cout << ;
        // std::cout << static_cast<int>(currencurrent_node->GetVisitCount()t_node->GetGameState().end) << "  " << static_cast<int>(current_node->GetGameState().shot) << std::endl;

        torch::Tensor ucb_value;
        
        torch::Tensor q = torch::where(current_node->GetChildVisitCount() > 0,
            current_node->GetChildSumValue() / current_node->GetChildVisitCount(), 0);
        torch::Tensor u = policy * std::sqrt(torch::sum(current_node->GetChildVisitCount()).item<int>() + 1) / (1 + current_node->GetChildVisitCount());

        // std::cout << torch::sum(current_node->GetChildVisitCount()).item<int>() << std::endl;

        if (current_node->GetGameState().GetNextTeam() == team) ucb_value = q + c_puct * u;
        else ucb_value = 1 - q + c_puct * u;

        if (dis(gen) < random_policy) ucb_value = torch::rand({1, policy_weight*policy_width*policy_rotation}); // * filt.reshape({1, policy_weight*policy_width*policy_rotation});


        // if (omp_get_thread_num() == 0) std::cout << torch::sum(q).item<float>() << torch::sum(policy).item<float>() << torch::sum(current_node->GetChildVisitCount()).item<int>();

        auto indices = std::get<1>(torch::topk(ucb_value, 1));

        // auto child_indices = current_node->GetChildIndices();

        // std::vector<int> matching_indices;
        // for (auto it = child_indices.begin(); it != child_indices.end(); ++it) {
        //     if (*it == indices.index({0, 0}).item<int>()) {
        //     matching_indices.push_back(std::distance(child_indices.begin(), it));
        //     }
        // }

        // std::vector<int>::iterator it;
        // if (!matching_indices.empty()) {
        //     // std::random_device rd;
        //     // std::mt19937 gen(rd());
        //     // std::uniform_int_distribution<> dis(0, matching_indices.size() - 1);
        //     // it = child_indices.begin() + matching_indices[dis(gen)];
        //     it = child_indices.begin() + matching_indices[current_node->GetChildVisitCount().index({0, indices.index({0, 0}).item<int>()}).item<int>() % matching_indices.size()];
        // } else {
        //     it = child_indices.end();
        // }

        // auto it = std::find(child_indices.begin(), child_indices.end(), indices.index({0, 0}).item<int>());
        
        // if (omp_get_thread_num() == 0) std::cout << indices.index({0, 0}).item<int>() << "  " << std::endl;

        auto next_node = current_node->GetChild(std::make_pair(
                indices.index({0, 0}).item<int>(),
                current_node->GetChildVisitCount().index({0, indices.index({0, 0}).item<int>()}).item<int>() % nSimulation
            ));

        // std::cout << indices.index({0, 0}).item<int>() << "  " << current_node->GetChildVisitCount().index({0, indices.index({0, 0}).item<int>()}).item<int>() % nSimulation << "  " << next_node << std::endl; 

        if (!(next_node) || (current_node->GetChildVisitCount().index({0, indices.index({0, 0}).item<int>()}).item<int>() < expand_threshold) || (current_node->GetGameState().shot == kShotPerEnd)) {
            queue_create_child[k] = current_node;
            queue_create_child_index[k] = std::make_pair(
                indices.index({0, 0}).item<int>(),
                current_node->GetChildVisitCount().index({0, indices.index({0, 0}).item<int>()}).item<int>() % nSimulation
            );
            flag_create_child[k] = true;
            SimulateMove(current_node, queue_create_child_index[k], k);

        } else {
            
            if (next_node->GetEvaluated()) result = next_node->GetValue();
            // else {
            //     queue_create_child[k] = current_node;
            //     queue_create_child_index[k] = indices.index({0, 0}).item<int>();
            //     flag_create_child[k] = true;
            // }

            // if (result != -1) updateNode(current_node, it - child_indices.begin(), result);

            if (next_node->GetEvaluated() & (next_node->GetGameState().shot > 0)) result = search(next_node, k);
        }
    }
    return result;
}


void Skip::searchById(UctNode* current_node, int k, std::pair<int, int> indices)
{
    queue_create_child[k] = current_node;
    queue_create_child_index[k] = indices;
    flag_create_child[k] = true;
    SimulateMove(current_node, queue_create_child_index[k], k);

}


void Skip::updateParent(UctNode* node, float value)
{
    if (node->GetParent()) {
        node->GetParent()->SetValue(value);
        node->GetParent()->SetCount(1);


        node->GetParent()->SetChildCountValue(node->GetIndices().first, 1, value);

        updateParent(node->GetParent(), value);
    }
}


void Skip::updateNodes()
{
    for (auto& node: queue_evaluate) {
        float value = node->GetValue();
        // node->SetValue(value);
        node->SetCount(1);
        updateParent(node, value);
    }
}


void Skip::updateCount(UctNode* node, std::pair<int, int> indices, int count)
{
    node->SetCount(count);

    node->SetChildCountValue(indices.first, count, 0);

    if (node->GetParent()) {
        updateCount(node->GetParent(), node->GetIndices(), count);
    }
}


void Skip::SimulateMove(UctNode* current_node, std::pair<int, int> indices, int k)
{
    dc::Move temp_move;
    dc::moves::Shot shot;
    dc::Vector2 velocity;


    temp_game_states[k] = current_node->GetGameState();

    velocity = utility::PixelToVelocity(indices.first % (policy_weight * policy_width) / policy_width, indices.first % (policy_weight * policy_width) % policy_width);

    if (indices.first / (policy_weight * policy_width) == 0) shot = {velocity, dc::moves::Shot::Rotation::kCW};
    else if (indices.first / (policy_weight * policy_width) == 1) shot = {velocity, dc::moves::Shot::Rotation::kCCW};
    else std::cerr << "shot error!";

    auto & current_player = *g_players[temp_game_states[k].shot / 4];
    temp_move = shot;

    dc::ApplyMove(g_game_setting, *g_simulators[k],
        current_player, temp_game_states[k], temp_move, std::chrono::milliseconds(0));

}


std::pair<torch::Tensor, std::vector<float>> Skip::EvaluateGameState(std::vector<dc::GameState> game_states, dc::GameSetting game_setting)
{
    torch::NoGradGuard no_grad; 
   
    // auto start = std::chrono::system_clock::now();
    // auto now = std::chrono::system_clock::now();

    utility::ModelInput model_input = utility::GameStateToInput(game_states, game_setting, device, dtype);
    // now = std::chrono::system_clock::now();
    // std::cout << "Evaluate: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;




    auto outputs = module.forward(model_input.inputs).toTuple();
    // now = std::chrono::system_clock::now();
    // std::cout << "Evaluate: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;

    int size = static_cast<int>(game_states.size());

    auto policy = F::softmax(outputs->elements()[0].toTensor().reshape({size, policy_weight * policy_width * policy_rotation}).to(torch::kCPU), 1);

    std::vector<std::vector<float>> win_rate_array(size, std::vector<float>(kShotPerEnd+1));

    // if (size==1) {
    //     auto sheet = model_input.inputs[0].toTensor().to(torch::kCPU);

    //     std::cout << sheet[0][0] << "\n" << sheet[0][1] << "\n";
    //     for (auto i=0; i<16; ++i){
    //         std::cout << sheet[0][i+2] << "\n";
    //     }
    // }
    // if (size!=1) std::cout << F::softmax(outputs, 1)[0] << std::endl;


    torch::Tensor score_prob = F::softmax(outputs->elements()[1].toTensor().to(torch::kCPU), 1);
    std::vector<float> win_prob(size, 0);

    for (auto n=0; n < size; ++n){
        int next_end = 10 - g_game_setting.max_end + model_input.end[n] + 1;
        if (game_states[n].shot == 0) {
            if (game_states[n].IsGameOver()) {
                win_prob[n] = team == game_states[n].game_result->winner;
            } else {
                int scorediff = model_input.score[n];
                if (scorediff > 9) scorediff = 9;
                else if (scorediff < -9) scorediff = -9;

                if (team == game_states[n].hammer) 
                win_prob[n] = win_table[scorediff+9][next_end-1];
                else win_prob[n] = 1 - win_table[scorediff+9][next_end-1];
            }
        } else {
            for (auto i=0; i < kShotPerEnd+1; ++i){
                int scorediff_after_end = model_input.score[n] + i - kShotPerEnd/2;
                if (scorediff_after_end > 9) scorediff_after_end = 9;
                else if (scorediff_after_end < -9) scorediff_after_end = -9;

                if (i > kShotPerEnd/2) {
                    win_rate_array[n][i] = 1 - win_table[9-scorediff_after_end][next_end];
                } else {
                    win_rate_array[n][i] = win_table[scorediff_after_end+9][next_end];
                }
            }

            for (auto i=0; i < kShotPerEnd+1; ++i) {
                win_prob[n] += score_prob.index({n, i}).item<float>() * win_rate_array[n][i];
            }
            if (team != game_states[n].hammer) win_prob[n] = 1 - win_prob[n];
        }
    }
    // now = std::chrono::system_clock::now();
    // std::cout << "Evaluate: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;

    // torch::Tensor win_rate = torch::from_blob(win_rate_array.data(), {size, kShotPerEnd+1});

    // torch::Tensor win_prob = at::sum(F::softmax(outputs, 1) * win_rate, 1).to(torch::kCPU);
    // now = std::chrono::system_clock::now();
    // std::cout << "Evaluate: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;


    return std::make_pair(policy, win_prob);
}


void Skip::EvaluateQueue()
{       
    int size = static_cast<int>(queue_evaluate.size());
    // std::cout << "Size: " << size << std::endl;

    std::vector<dc::GameState> game_states;
    // game_states.resize(size);

    // #pragma omp parallel for
    // for (auto i=0; i<size; ++i) {
    //     // std::cout << queue_evaluate[i]->GetIndices().first << "  " << queue_evaluate[i]->GetIndices().second << std::endl;
    //     game_states[i] = queue_evaluate[i]->GetGameState();
    // }

    for (auto i=0; i<size; ++i) {
        // std::cout << queue_evaluate[i]->GetIndices().first << "  " << queue_evaluate[i]->GetIndices().second << "  ";
        // std::cout << queue_evaluate[i] << std::endl;
        game_states.push_back(queue_evaluate[i]->GetGameState());
    }

    // std::cout << "Get Game State" << std::endl;

    auto policy_value = EvaluateGameState(game_states, g_game_setting);

    // std::cout << "Evaluate Game State" << std::endl;

    // torch::Tensor policy = torch::rand({size, policy_weight * policy_width * policy_rotation}).to(torch::kCPU);
    // torch::Tensor value = torch::rand({size});

    for (int i=0; i<size; ++i) {
        // std::cout << static_cast<int>(game_states[i].end) << static_cast<int>(game_states[i].shot) << std::endl;
        queue_evaluate[i]->SetEvaluatedResults(policy_value.first.index({i}), policy_value.second[i]);
        // queue_evaluate[i]->SetFilter(utility::createFilter(game_states[i], g_game_setting));
    }

    // std::cout << "Set Evaluated Results" << std::endl;
}


dc::Move Skip::command(dc::GameState const& game_state)
{
    // TODO AIを作る際はここを編集してください
    auto start = std::chrono::system_clock::now();

    dc::GameState current_game_state = game_state;

    torch::NoGradGuard no_grad; 

    // 現在の局面を評価
    auto policy_value = EvaluateGameState({current_game_state}, g_game_setting);


    // auto sheet = utility::GameStateToInput({current_game_state}, g_game_setting, torch::kCPU, dtype).inputs[0].toTensor();
    // // std::cout << sheet[0][0] << sheet[0][1] << std::endl;
    // for (auto i=0; i<16; ++i){
    //     std::cout << sheet[0][i+2][0][0].item<int>();
    // }
    // for (auto i=0; i < 2; ++i){
    //     for (auto j=0; j < utility::height; ++j) {
    //         for (auto k=0; k < utility::width; ++k) {
    //             std::cout << sheet.index({0, i, j, k}).item<float>();
    //         }
    //         std::cout << "\n";
    //     }
    //     std::cout << "\n";
    //     std::cout << "\n";
    // }

    // for (auto i=0; i < 16; ++i) {
    //     std::cout << sheet[0][i+2][0][0].item<float>();
    // }
    // auto policy = F::softmax(current_outputs.elements()[0].toTensor().reshape({1, 18700}).to(torch::kCPU), 1);

    // auto filt = utility::createFilter(current_game_state, g_game_setting);

    // root node
    std::unique_ptr<UctNode> root_node(new UctNode());
    root_node->SetGameState(current_game_state);
    root_node->SetEvaluatedResults(policy_value.first.index({0}), policy_value.second[0]);
    // root_node->SetFilter(filt);

    // for (auto i=0; i < policy_rotation; ++i){
    //     for (auto j=0; j < policy_weight; ++j) {
    //         for (auto k=0; k < policy_width; ++k) {
    //             std::cout << std::setw(2) << std::setfill('0') << std::setprecision(0) << static_cast<int>(policy_value.first.index({0, utility::Id3d1d(i, j, k)}).item<float>() * 10000) << " ";
    //         }
    //         std::cout << std::endl;
    //     }
    //     std::cout << std::endl;
    // }

    // std::cout << torch::rand({policy_rotation, policy_weight, policy_width}) * filt << std::endl;

    int count = 0;
    auto now = std::chrono::system_clock::now();
    auto a = std::chrono::system_clock::now();
    auto b = std::chrono::system_clock::now();
    while ((now - start < limit)){
        a = std::chrono::system_clock::now();
        if (current_game_state.shot + 1 == kShotPerEnd) {
        // if (current_game_state.shot >= 0) {
        // if (false) {
            if (count >= policy_rotation*policy_weight*policy_width*nSimulation) break;
            #pragma omp parallel for
            for (auto i = 0; i < nBatchSize; ++i) {
                searchById(root_node.get(), i, std::make_pair((count + i) % (policy_rotation*policy_weight*policy_width), (count + i) / (policy_rotation*policy_weight*policy_width)));

                if (flag_create_child[i]) {
                    updateCount(queue_create_child[i], queue_create_child_index[i], virtual_loss);
                }
            }

        } else {
            // while (queue_evaluate.size() < nBatchSize) {
            #pragma omp parallel for
            for (auto i = 0; i < nLoop; ++i) {
                // std::cout << i << "  ";
                search(root_node.get(), i);

                // #pragma omp atomic
                if (flag_create_child[i]) {
                    updateCount(queue_create_child[i], queue_create_child_index[i], virtual_loss);
                }

                if (std::accumulate(std::begin(flag_create_child), std::end(flag_create_child), 0) >= nBatchSize) continue;
            }
        }
        b = std::chrono::system_clock::now();
        // std::cout << "Search: " << std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count() << " msec" << "\n";
        

        a = std::chrono::system_clock::now();
        for (auto i = 0; i < nLoop; ++i) {
            if (flag_create_child[i]) {
                // std::cout << queue_create_child_index[i].first << "  " << queue_create_child_index[i].second << std::endl;
                queue_create_child[i]->CreateChild(queue_create_child_index[i]);
                queue_create_child[i]->GetChild(queue_create_child_index[i])->SetGameState(temp_game_states[i]);

                updateCount(queue_create_child[i], queue_create_child_index[i], -1*virtual_loss);

                if (queue_evaluate.size() < nBatchSize) {
                    queue_evaluate.push_back(queue_create_child[i]->GetChild(queue_create_child_index[i]));
                    // std::cout << i << ":  ";
                    // std::cout << queue_create_child_index[i].first << "  " << queue_create_child_index[i].second << ";  ";
                    // std::cout << queue_create_child[i]->GetChild(queue_create_child_index[i])->GetIndices().first << "  " << queue_create_child[i]->GetChild(queue_create_child_index[i])->GetIndices().second << ";  ";
                    // std::cout << queue_create_child[i]->GetChild(queue_create_child_index[i]) << std::endl;
                }
            }
        }
        b = std::chrono::system_clock::now();
        // std::cout << "Expand: " << std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count() << " msec" << "\n";

        // for (auto ptr : queue_create_child) {
        //     ptr.reset();
        // }
        #pragma omp parallel for
        for (auto i=0; i < nLoop; ++i) {
            flag_create_child[i] = false;
        }


        a = std::chrono::system_clock::now();

        count += queue_evaluate.size();
        EvaluateQueue();
        b = std::chrono::system_clock::now();
        // std::cout << "Evaluate: " << std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count() << " msec" << "\n";

        a = std::chrono::system_clock::now();

        updateNodes();
        queue_evaluate.clear();
        b = std::chrono::system_clock::now();
        // std::cout << "Update: " << std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count() << " msec" << "\n";


        // torch::Tensor ucb_value;
        // torch::Tensor q = torch::where(root_node->GetChildVisitCount() > 0,
        // root_node->GetChildSumValue() / root_node->GetChildVisitCount(), 0);
        // torch::Tensor u = root_node->GetPolicy() * std::sqrt(root_node->GetVisitCount()) / (1 + root_node->GetChildVisitCount());

        // ucb_value = q + u;
        // for (auto i=0; i < utility::policy_rotation; ++i){
        //     for (auto j=0; j < utility::policy_weight; ++j) {
        //         for (auto k=0; k < utility::policy_width; ++k) {
        //             std::cout << ucb_value.index({0, i*utility::policy_weight*utility::policy_width + j*utility::policy_width + k}).item<float>() << " ";
        //         }
        //         std::cout << "\n";
        //     }
        //     std::cout << "\n";
        //     std::cout << "\n";
        // }

        now = std::chrono::system_clock::now();
    }
    // GPUのキャッシュをクリア
    c10::cuda::CUDACachingAllocator::emptyCache();

    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    std::cout << count << "  " << root_node->GetVisitCount() << " simulations in " << msec << " msec" << std::endl;

    // std::vector<int> child_indices = root_node->GetChildIndices();
    // std::vector<float> values;
    // std::array<std::array<std::array<int, policy_width>, policy_weight>, policy_rotation> win_rate;
    // std::fill(&win_rate[0][0][0], &win_rate[0][0][0] + policy_rotation * policy_weight * policy_width, 0);
    // if (current_game_state.shot + 1 == kShotPerEnd) {
        // values.resize(policy_width*policy_weight*policy_rotation, 0);
        // for (auto i=0; i < child_indices.size(); ++i) {
            // values[child_indices[i]] += root_node->GetChildById(child_indices[i])->GetCountValue() / nSimulation;

            // win_rate[child_indices[i] / (policy_width*policy_weight)][(child_indices[i] % (policy_width*policy_weight)) / policy_width][child_indices[i] % policy_width] += root_node->GetChildById(child_indices[i])->GetVisitCount();
        // }
        // for (auto i=0; i < policy_rotation; ++i){
        //     for (auto j=0; j < policy_weight; ++j) {
        //         for (auto k=0; k < policy_width; ++k) {
        //             win_rate[i][j][k] = values[utility::Id3d1d(i, j, k)];
        //         }GetCountValue
        //     }
        // }
    // } else {
        // for (auto index: child_indices) {
            // values.push_back(root_node->GetChild(index)->GetCountValue());
            // win_rate[index / (policy_width*policy_weight)][(index % (policy_width*policy_weight)) / policy_width][index % policy_width] += root_node->GetChild(index)->GetVisitCount();
        // }
        // for (auto index: child_indices) {
        //     win_rate[index / (policy_weight * policy_width)][index % (policy_weight * policy_width) / policy_width][index % (policy_weight * policy_width) % policy_width] = root_node->GetChild(index)->GetCountValue();
        // }

    // }


    // for (auto i=0; i < policy_rotation; ++i){
    //     // for (auto j=0; j < policy_weight; ++j) {
    //         int j = 0;
    //         for (auto k=0; k < policy_width; ++k) {
    //             std::cout << std::setw(2) << std::setfill('0') << std::setprecision(0) << static_cast<int>(win_rate[i][j][k] * 100) << " ";
    //         }
    //         std::cout << "\n";
    //     // }
    //     std::cout << "\n";
    //     std::cout << "\n";
    // }

    // if (current_game_state.shot % 2 == 0) {
    //     for (auto i=0; i < values.size(); ++i) {
    //         values[i] = 1 - values[i];
    //     }
    // }

    // for (auto i=0; i < policy_rotation; ++i){
    //     for (auto j=0; j < policy_weight; ++j) {
    //         for (auto k=0; k < policy_width; ++k) {
    //             std::cout << std::setw(2) << std::setfill('0') << std::setprecision(0) << static_cast<int>((torch::where(root_node->GetChildVisitCount() > 0, root_node->GetChildSumValue() / root_node->GetChildVisitCount(), 0) + policy_value.first).index({0, utility::Id3d1d(i, j, k)}).item<float>() * 100) << " ";
    //             // std::cout << std::setw(2) << std::setfill('0') << std::setprecision(0) << static_cast<int>(root_node->GetChildVisitCount().index({0, utility::Id3d1d(i, j, k)}).item<int>()) << " ";
    //         }
    //         std::cout << std::endl;
    //     }
    //     std::cout << std::endl;
    // }



    // int pixel_id = child_indices[static_cast<int>(std::distance(values.begin(), std::max_element(values.begin(), values.end())))];

    int pixel_id = std::get<1>(torch::topk(torch::where(root_node->GetChildVisitCount() >= min_visit, root_node->GetChildSumValue() / root_node->GetChildVisitCount(), 0) + policy_value.first, 1)).index({0, 0}).item<int>();

    // std::cout << pixel_id << ":   " << pixel_id / (policy_weight * policy_width) << ", " << pixel_id % (policy_weight * policy_width) / policy_width << ", " << pixel_id % (policy_weight * policy_width) % policy_width << std::endl;

    dc::moves::Shot::Rotation rotation;
    if (pixel_id / (policy_weight * policy_width) == 0) rotation = dc::moves::Shot::Rotation::kCW;
    else rotation = dc::moves::Shot::Rotation::kCCW;

    dc::moves::Shot shot = {utility::PixelToVelocity(pixel_id % (policy_weight * policy_width) / policy_width, pixel_id % (policy_weight * policy_width) % policy_width), rotation};
    dc::Move move = shot;

    // std::cout << std::accumulate(std::begin(values), std::end(values), 0.f) << std::endl;
    // if (std::accumulate(std::begin(values), std::end(values), 0.f) < 1e-6f) move = dc::moves::Concede();

    // move = dc::moves::Concede();

    return move;
}

