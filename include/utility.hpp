#ifndef UTILITY_HPP
#define UTILITY_HPP

#include "config.hpp"
#include "digitalcurling3/digitalcurling3.hpp"

namespace dc = digitalcurling3;

using namespace config;

namespace utility
{


    // ストーンの座標からシートの画像のピクセルに変換する
    std::pair<int, int> PositionToPixel(dc::Vector2 position)
    {
        std::pair<int, int> pixel;

        pixel.first = static_cast<int>(round((position.y - 32.004)*m_to_inch*dpi));
        pixel.second = width/2 - static_cast<int>(round(position.x*m_to_inch*dpi));

        return pixel;
    }


    // policyの画像のピクセルからショットの速度に変換する
    dc::Vector2 PixelToVelocity(int i, int j)
    {
        std::array<float, policy_weight> velocity_array{{2.23, 2.27, 2.31,
            2.35, 2.37, 2.385, 2.4, 2.415, 2.43, 2.45,
            2.485, 2.535, 2.6, 3., 3.4, 3.8,}};

        return dc::Vector2(velocity_array[i] * std::sin(std::atan(-(j - policy_width/2) * 0.0254 * 5 * one_over_to_tee)), velocity_array[i] * std::cos(std::atan(-(j - policy_width/2) * 0.0254 * 5 * one_over_to_tee)));
    }


    class ModelInput {
        public:
            std::vector<torch::jit::IValue> inputs;
            std::vector<int> end;
            std::vector<int> score;

            ModelInput to(torch::Device device) {
                ModelInput model_input;
                std::vector<torch::jit::IValue> inputs_copy;

                for (auto input: inputs){
                    inputs_copy.push_back(input.toTensor().to(device));
                }

                model_input.inputs = inputs_copy;
                model_input.end = end;
                model_input.score = score;

                return model_input;

            }
    };


    int Id4d1d(int i, int l, int n, int m)
    {
        return m + n*width + l*width*height + i*width*height*nChannel;
    }

    // GameStateからモデルに入力する形式に変換する
    ModelInput GameStateToInput(std::vector<dc::GameState> const game_states, dc::GameSetting game_setting, torch::Device device, torch::ScalarType dtype)
    {
        // auto start = std::chrono::system_clock::now();
        // auto now = std::chrono::system_clock::now();

        
        std::vector<torch::jit::IValue> inputs;
        int const size = static_cast<int>(game_states.size());

        // torch::Tensor sheet = torch::zeros({static_cast<int>(game_states.size()), nChannel, height, width}).to(device);
        std::vector<float> sheet_array(width*height*nChannel*size, 0);
        // sheet_array.resize(size);
        std::vector<int> end(size, 0);
        std::vector<int> score(size, 0);
        // torch::Tensor shot = torch::zeros({static_cast<int>(game_states.size()), 1}).to(device);

        for (auto i=0; i < size; ++i){
            if (game_states[i].IsGameOver()) continue; // 試合終了していたらスキップ

            // shot.index({i, 0}) = (game_states[i].kShotPerEnd - game_states[i].shot) / 16.f;
            score[i] = (game_states[i].GetTotalScore(game_states[i].hammer) - game_states[i].GetTotalScore(dc::GetOpponentTeam(game_states[i].hammer)));
            if (game_states[i].end < game_setting.max_end){
                end[i] = game_states[i].end;
            } else {
                end[i] = 9; // エキストラエンドは10エンドと同じ
            }
            // std::cout << game_states[i].kShotPerEnd << std::endl;
            // now = std::chrono::system_clock::now();
            // std::cout << "Input: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;

            
            for (auto l=0; l <= game_states[i].shot; ++l){
                for (auto n=0; n < height; ++n){
                    for (auto m=0; m < width; ++m){
                        sheet_array[Id4d1d(i, l+2, n, m)] = 1.;
                    }
                }  
            }
            for (auto l=game_states[i].shot+1; l < game_states[i].kShotPerEnd; ++l){
                for (auto n=0; n < height; ++n){
                    for (auto m=0; m < width; ++m){
                        sheet_array[Id4d1d(i, l+2, n, m)] = 0.;
                    }
                }  
            }
            for (auto l=0; l < 2; ++l){
                for (auto n=0; n < height; ++n){
                    for (auto m=0; m < width; ++m){
                        sheet_array[Id4d1d(i, l, n, m)] = 0.;
                    }
                }  
            }
            // now = std::chrono::system_clock::now();
            // std::cout << "Input: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;

            for (size_t team_stone_idx = 0; team_stone_idx < game_states[i].kShotPerEnd / 2; ++team_stone_idx) {
                auto const& stone = game_states[i].stones[static_cast<size_t>(dc::GetOpponentTeam(game_states[i].hammer))][team_stone_idx];
                // std::cout << stone_nohammer->position.x << std::endl;
                if (stone) {
                    std::pair <int, int> pixel = PositionToPixel(stone->position);
                    // std::cout << pixel << std::endl;
                    sheet_array[Id4d1d(i, 0, pixel.first, pixel.second)] = 1.;
                }
            }
            for (size_t team_stone_idx = 0; team_stone_idx < game_states[i].kShotPerEnd / 2; ++team_stone_idx) {
                auto const& stone = game_states[i].stones[static_cast<size_t>(game_states[i].hammer)][team_stone_idx];
                if (stone) {
                    std::pair <int, int> pixel = PositionToPixel(stone->position);
                    // std::cout << pixel << std::endl;
                    sheet_array[Id4d1d(i, 1, pixel.first, pixel.second)] = 1.;
                }
            }
        }
        torch::Tensor sheet = torch::from_blob(sheet_array.data(), {size, nChannel, height, width});

        // now = std::chrono::system_clock::now();
        // std::cout << "Input: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << " msec" << std::endl;

        inputs.push_back(sheet.to(dtype).to(device));
        // inputs.push_back(end);
        // inputs.push_back(score);
        // inputs.push_back(shot);

        ModelInput model_input;

        model_input.inputs = inputs;
        model_input.end = end;
        model_input.score = score;

        return model_input;
    }


std::array<std::array<std::array<bool, policy_width>, policy_weight>, policy_rotation> createFilter(dc::GameState game_state, dc::GameSetting game_setting)
{
    // torch::Tensor filt = torch::zeros({policy_rotation, policy_weight, policy_width}).to("cpu");
    std::array<std::array<std::array<bool, policy_width>, policy_weight>, policy_rotation> filt;
    filt.fill({});

    int min_velocity = 0;
    if (game_state.shot+1 == game_state.kShotPerEnd) min_velocity = 3; // ラストショットはハウスに届かないショットを投げても意味がない

    for (auto i=0; i < policy_weight; ++i){
        for (auto j=0; j < policy_width; ++j){
            if ((min_velocity <= i) && (i < policy_weight) && (j < 14)) filt[1][i][j] = 1;
            if ((min_velocity <= i) && (i < policy_weight) && (j >= 18)) filt[0][i][j] = 1;

            // if ((i < 37) && (j < 139)) filt.index({0, i, j}) = 0; // block siipdde guard
            // if ((i < 37) && (j >= 48)) filt.index({1, i, j}) = 0;
        }
    }


    // for (size_t team_stone_idx = 0; team_stone_idx < game_state.kShotPerEnd / 2; ++team_stone_idx) {
    //     auto const& stone_hammer = game_state.stones[static_cast<size_t>(game_state.hammer)][team_stone_idx];
    //     auto const& stone_nohammer = game_state.stones[static_cast<size_t>(dc::GetOpponentTeam(game_state.hammer))][team_stone_idx];
    //     if (stone_hammer) {
    //         std::pair <int, int> pixel = PositionToPixel(stone_hammer->position);
    //         // std::cout << pixel << std::endl;
    //         for (auto i=0; i < 50; ++i){
    //             for (auto j=0; j < 187; ++j){
    //                 if ((4*(i - 50) >= j - pixel.second + (pixel.first - 252)/20) && (4*(i - 50) <= j - pixel.second + (pixel.first - 252)/20 + 40)) {
    //                     filt.index({1, i, j}) = 1;
    //                 }
    //                 if ((-4*(i - 50) <= j - (187 - (pixel.second - (pixel.first - 252)/20))) && (-4*(i - 50) >= j - (187 - (pixel.second - (pixel.first - 252)/20 - 40)))) {
    //                     filt.index({0, i, j}) = 1;
    //                 }
    //             }
    //         }
    //     }
    //     if (stone_nohammer) {
    //         std::pair <int, int> pixel = PositionToPixel(stone_nohammer->position);
    //         // std::cout << pixel << std::endl;
    //         for (auto i=0; i < 50; ++i){
    //             for (auto j=0; j < 187; ++j){
    //                 if ((4*(i - 50) >= j - pixel.second + (pixel.first - 252)/20) && (4*(i - 50) <= j - pixel.second + (pixel.first - 252)/20 + 40)) {
    //                     filt.index({1, i, j}) = 1;
    //                 }
    //                 if ((-4*(i - 50) <= j - (187 - (pixel.second - (pixel.first - 252)/20))) && (-4*(i - 50) >= j - (187 - (pixel.second - (pixel.first - 252)/20 - 40)))) {
    //                     filt.index({0, i, j}) = 1;
    //                 }
    //             }
    //         }
    //     }
    // }

    return filt;
}

int Id3d1d(int i, int j, int k)
{
    return k + j*policy_width + i*policy_width*policy_weight;
}


std::array<bool, policy_width*policy_weight*policy_rotation> createFilter()
{
    std::array<bool, policy_width*policy_weight*policy_rotation> filt{{}};
    // filt.fill({});

    int min_velocity = 0;

    for (auto i=0; i < policy_weight; ++i){
        for (auto j=0; j < policy_width; ++j){
            // if ((min_velocity <= i) && (i < policy_weight) && (j < 14)) filt[Id3d1d(1, i, j)] = true;
            if (j < 14) filt[Id3d1d(1, i, j)] = true;
            else filt[Id3d1d(1, i, j)] = false;
            // if (/*(min_velocity <= i) && (i < policy_weight) && */(j >= 18)) filt[Id3d1d(0, i, j)] = true;
            if (j >= 18) filt[Id3d1d(0, i, j)] = true;
            else filt[Id3d1d(0, i, j)] = false;

            if (i > 10) {
                filt[Id3d1d(0, i, j)] = true;
                filt[Id3d1d(1, i, j)] = true;
            }

            // if ((i < 37) && (j < 139)) filt.index({0, i, j}) = 0; // block siipdde guard
            // if ((i < 37) && (j >= 48)) filt.index({1, i, j}) = 0;
        }
    }
    return filt;
}

    
} // namespace utility

#endif // UTILITY_HPP