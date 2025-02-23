#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace config {
    const int nSimulation = 8; // 1つのショットに対する誤差を考慮したシミュレーション回数
    const int nBatchSize = 512; // CNNで推論するときのバッチサイズ
    const int nLoop = 1024; // 
    // const int nCandidate = 10000; // シミュレーションするショットの最大数。制限時間でシミュレーションできる数よりも十分大きく取る

    const int virtual_loss = 1;
    const float c_puct = 10;

    const float random_policy = 0.15; // rate of randomly selecting move instead of selecting by policy

    const int expand_threshold = nSimulation; // max value of number of simulation for each child node

    const int min_visit = 6; // minimum number of visit for each child node to select move


    int const policy_weight = 16;
    int const policy_width = 32;
    int const policy_rotation = 2;
}

#endif // CONFIG_HPP