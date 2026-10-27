// Prints vanilla FastNoise values used by Ashlands for a fixed point set.
using System;
using System.Globalization;
class P {
    static void Main() {
        var n = new FastNoise(-2067736654);
        n.SetNoiseType(FastNoise.NoiseType.Cellular);
        n.SetCellularDistanceFunction(FastNoise.CellularDistanceFunction.Euclidean);
        n.SetCellularReturnType(FastNoise.CellularReturnType.Distance);
        n.SetFractalOctaves(2);
        n.SetSeed(0);
        var ci = CultureInfo.InvariantCulture;
        for (int i = 0; i < 20; i++) {
            double x = 100000 + i * 137.37, y = 100000 - i * 91.13;
            Console.WriteLine(string.Format(ci, "{0:R} {1:R} {2:R} {3:R}", n.GetCellular(x * 0.33, y * 0.33), n.GetCellular(x * 8, y * 8), n.GetSimplexFractal(x * 0.075, y * 0.075), n.GetCellular(x, y)));
        }
    }
}
