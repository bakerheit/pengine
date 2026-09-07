# Tractor and trailer

The Harrow Hauler semi and Harrow Freight Trailer are parked at Camber Air Cargo, at O'Haven International Airport. F1 → Teleport → CAMBER AIR CARGO gets you beside the truck. The tractor can also be selected through F1 → Vehicle → Choose Car → HARROW → HAULER SEMI.

Enter the tractor with E / controller A. Back its fifth wheel underneath the trailer's front pin, straighten up, stop, and press **T / D-pad Right**. Press the same button while stopped on level ground to drop the trailer. Coupling requires the pin to be within 0.65 m horizontally and 0.25 m vertically, alignment within about 11 degrees, and speed below 0.35 m/s. The HUD explains a rejected attempt.

The trailer follows its rear bogie through forward and reverse turns. It has moving shared wheels, retractable landing legs, connected rear lights, and a 65-degree articulation limit. The attached rig accelerates and brakes more slowly. Swept collision checks stop the rig at walls, parked vehicles and posts; a separate 3D clearance check protects the cab on crests. Traffic avoids the trailer and receives its usual collision response on contact.

Dropping preserves its world pose. It remains available for another pickup, including after saving and loading. Checkpoints now use version 2 and still read version 1; existing vehicle IDs remain stable. Switching vehicles or using developer teleport/respawn drops an attached trailer where it is. This is an arcade articulated rig; it does not simulate cargo, air lines, free-rolling detached trailers or independent trailer damage.

Assets and reproduction instructions: [Harrow semi asset contract](assets/harrow-semi.md).

## Validation

```sh
cmake --build build --target apricot tractor_trailer_tests -j8
ctest --test-dir build --output-on-failure -R 'tractor_trailer_tests|ui_flow_tests|save_game_tests|input_latch_tests'
build/bin/apricot --trailer-check --frames 300 --clear \
  --screenshot build/trailer-gameplay.bmp --log /tmp/apricot-trailer-check.log
```

The bounded host check backs under the pin through real vehicle physics, rejects moving coupling, brakes, couples, pulls away, drops, drives away, and couples again. It also saves and restores both attached and dropped states through the real host checkpoint path using a temporary file. Unit coverage includes four compass headings, turn tracking, reverse articulation, thin obstacles, cab clearance on crests, parked poses, checkpoint round trips and old checkpoint IDs. Asset Lab screenshots verify the cooked body and the game's shared tires.

Validated on 2026-09-05: build and purity guard passed; 100/103 full-suite tests passed. The remaining failures are `route1_traffic_tests`, `road_name_tests`, and the existing duplicate-pitch assertion in `audio_vehicle_runtime_tests`. Trailer, input/replay, menu, save, mesh/lamp and bus-regression suites passed. Both daytime and nighttime host runs completed 300 frames with a clean GL queue.
