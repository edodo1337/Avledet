// Same points through Avledet's FastNoise port.
#include <cstdio>
#include "FastNoise.h"
int main() {
    avledet::util::FastNoise n(-2067736654);
    n.SetNoiseType(avledet::util::FastNoise::NoiseType::Cellular);
    n.SetCellularDistanceFunction(avledet::util::FastNoise::CellularDistanceFunction::Euclidean);
    n.SetCellularReturnType(avledet::util::FastNoise::CellularReturnType::Distance);
    n.SetFractalOctaves(2);
    n.SetSeed(0);
    for (int i = 0; i < 20; i++) {
        double x = 100000 + i * 137.37, y = 100000 - i * 91.13;
        std::printf("%.17g %.17g %.17g %.17g\n", n.GetCellular(x * 0.33, y * 0.33), n.GetCellular(x * 8, y * 8), n.GetSimplexFractal(x * 0.075, y * 0.075), n.GetCellular(x, y));
    }
}
