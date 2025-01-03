#ifndef SKIP_HPP
#define SKIP_HPP

#include <vector>
#include <utility>

#include "uctnode.hpp"
#include "digitalcurling3/digitalcurling3.hpp"

namespace dc = digitalcurling3;

const int nSimulation = 5; // 1つのショットに対する誤差を考慮したシミュレーション回数
const int nBatchSize = 512; // CNNで推論するときのバッチサイズ
const int nLoop = 1024; // 
// const int nCandidate = 10000; // シミュレーションするショットの最大数。制限時間でシミュレーションできる数よりも十分大きく取る

const int virtual_loss = 1;
const float c_puct = 10;

const float random_policy = 0.3; // rate of randomly selecting move instead of selecting by policy

const int expand_threshold = 1; // max value of number of simulation for each child node

const int min_visit = 4; // minimum number of visit for each child node to select move

class Skip
{
    public:
        Skip();

        void OnInit(dc::Team const, dc::GameSetting const&,     std::unique_ptr<dc::ISimulatorFactory>,     std::array<std::unique_ptr<dc::IPlayerFactory>, 4>, std::array<size_t, 4> & );

        float search(UctNode*, int);
        void searchById(UctNode*, int, int);

        void updateNodes();
        void updateParent(UctNode*, float);

        void updateCount(UctNode*, int, int);

        void SimulateMove(UctNode*, int, int);
        std::pair<torch::Tensor, std::vector<float>> EvaluateGameState(std::vector<dc::GameState>, dc::GameSetting);
        void EvaluateQueue();

        dc::Move command(const dc::GameState&);

    private:
        dc::Team team;
        torch::jit::script::Module module;
        dc::GameSetting g_game_setting;
        std::array<std::unique_ptr<dc::ISimulator>, nLoop> g_simulators;
        std::unique_ptr<dc::ISimulatorStorage> g_simulator_storage;
        std::array<std::unique_ptr<dc::IPlayer>, 4> g_players;
        std::chrono::duration<double> limit;
        std::vector<std::vector<double>> win_table;
        torch::Device device;

        torch::ScalarType dtype;

        std::vector<UctNode*> queue_evaluate;
        std::vector<int> queue_simulate;

        std::array<UctNode*, nLoop> queue_create_child;
        std::array<int, nLoop> queue_create_child_index;
        std::array<bool, nLoop> flag_create_child;

        std::array<dc::GameState, nLoop> temp_game_states;

        int kShotPerEnd;

        torch::Tensor filt;

// torch::jit::script::Module module; // モデル

// // シミュレーション用変数
// dc::GameSetting g_game_setting;
// std::unique_ptr<dc::ISimulator> g_simulator;
// std::array<std::unique_ptr<dc::ISimulator>, nLoop> g_simulators;
// std::array<std::unique_ptr<dc::IPlayer>, 4> g_players;

// std::chrono::duration<double> limit; // 考慮時間制限




};

#endif // SKIP_HPP
