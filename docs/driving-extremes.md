# Extreme driving characterization

`driving_extremes_tests` runs the same six extreme input sequences through all
nine F1 driving profiles using the real 120 Hz vehicle simulation and terrain
collider. It exists to separate three steps that are easy to muddle together:

1. Measure what the physics does now.
2. Decide which outcomes are desirable for each handling style.
3. Turn accepted outcomes into regression limits, then refine the physics.

Run it directly to see the full matrix:

```sh
cmake --build build --target driving_extremes_tests -j 8
./build/bin/driving_extremes_tests
```

The scenarios are full lock at 30 m/s, panic brake plus turn at 40 m/s,
repeated left/right flicks at 35 m/s, a handbrake entry at 28 m/s, a turn plus
violent outside-wheel curb trip at 30 m/s, and a soaked-surface panic
brake-turn at 35 m/s.

The table reports entry and exit speed, peak roll and pitch angle, peak body
slip, accumulated yaw, minimum grounded wheels, total airborne time, stopping
time, and a compact observed outcome. `PLANTED`, `STOPPED`, `NO_STOP`, `SLIDE`,
`SPIN`, `WHEEL_LIFT`, `AIRBORNE`, and `ROLLOVER` are descriptions, not automatic
judgments. `NO_STOP` means the car stayed controlled but did not reach 1 m/s
inside that scenario's three-second panic-stop window.

For now the suite fails only when physics becomes non-finite, hits its emergency
speed ceiling, or gives a different result on an exact repeat. After reviewing
the matrix, accepted behavior should be encoded as scenario-and-profile bands.
Examples: Classic GTA may require no inversion and at least three wheels down
during a panic brake-turn; Drift may intentionally allow much more slip and yaw
during the handbrake scenario, but still reject a rollover.
