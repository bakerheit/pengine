# Vehicle performance

[Vehicle directory](README.md)

Top speed and **0–60 mph in X.X s** are recorded on each model page. The table below measures the current game physics using **Classic GTA**, the default driving preset. These are gameplay figures; they do not establish fictional factory specifications. Other driving presets can produce different results.

## Measurement conditions

- Production `player_model_tuning` and `step_vehicle`, run without graphics, traffic or collisions with other vehicles.
- Flat, dry `Surface::Rock` test ground, with normal undamaged vehicle state and default grip. This is the same ground material used by the existing police performance rig.
- Two seconds to settle the suspension with the brake held, followed by full throttle from rest, zero steering and the automatic gearbox.
- Fixed step: 120 Hz. Speed is measured forward along the vehicle. 60 mph is exactly 26.8224 m/s; threshold crossing is interpolated between steps and the result is shown to one decimal place.
- Top speed is the final five-second mean once three successive five-second means change by less than 0.02 m/s, with convergence checks starting after 30 seconds. The run ends after at most 180 seconds.
- A vehicle that does not settle reports its observed peak explicitly. A missing acceleration time means it did not reach 60 mph within the run.

The saved run contains **38 catalog vehicles**: 37 settled top speeds and 38 measured 0–60 times. Fang Venom did not settle within 180 seconds. Ashworth Vale and Regalia Borough are unimplemented concepts and have no measured performance.

These results come from the local working tree, including ongoing vehicle work. [Raw measurements](performance.csv) and the [source snapshot](performance-snapshot.json) preserve the conditions and input hashes. This benchmark measures straight-line physics; it does not certify the current rendered model, handling on a city route or integration status.

## Roster

| Vehicle | Top speed | 0–60 mph |
|---|---|---|
| [Alder Pip](Alder/Pip.md) | 112.0 mph | 4.5 s |
| [Alder Ridge](Alder/Ridge.md) | 125.2 mph | 4.6 s |
| [Alder Wayfarer](Alder/Wayfarer.md) | 134.8 mph | 4.8 s |
| [Ashworth Vale](Ashworth/Vale.md) | Not specified — concept only | Not measured — concept only |
| [BWC 360](BWC/360.md) | 176.9 mph | 5.2 s |
| [Ember GT](Ember/GT.md) | 221.5 mph | 3.7 s |
| [Fang Venom](Fang/Venom.md) | Unsettled; 129.1 mph peak observed | 3.8 s |
| [GLM Lunge](GLM/Lunge.md) | 184.6 mph | 3.6 s |
| [GLM Meridian](GLM/Meridian.md) | 146.5 mph | 5.1 s |
| [GLM Zip](GLM/Zip.md) | 173.4 mph | 3.6 s |
| [Harrow Cityliner Bus](Harrow/Cityliner%20Bus.md) | 91.5 mph | 4.9 s |
| [Harrow Hauler Semi](Harrow/Hauler%20Semi.md) | 105.4 mph | 3.9 s |
| [Harrow Hookline](Harrow/Hookline.md) | 138.5 mph | 5.0 s |
| [Harrow Parcel](Harrow/Parcel.md) | 115.6 mph | 4.3 s |
| [Harrow Workman](Harrow/Workman.md) | 125.2 mph | 4.2 s |
| [Karlsdale Lux](Karlsdale/Lux.md) | 126.4 mph | 4.4 s |
| [Karlsdale Regent Eight](Karlsdale/Regent%20Eight.md) | 83.5 mph | 6.2 s |
| [Karlsdale Six Sedan](Karlsdale/Six%20Sedan.md) | 93.1 mph | 5.7 s |
| [Legacy Car 5-Next Patrol](Legacy/Car%205-Next%20Patrol.md) | 170.1 mph | 4.2 s |
| [Legacy Car 5-Next](Legacy/Car%205-Next.md) | 163.7 mph | 3.8 s |
| [Legacy Car 5](Legacy/Car%205.md) | 160.5 mph | 3.9 s |
| [Legacy Car 8 Ambulance](Legacy/Car%208%20Ambulance.md) | 128.4 mph | 4.3 s |
| [Legacy Car 8](Legacy/Car%208.md) | 134.9 mph | 3.9 s |
| [Municipal Ambulance](Municipal/Ambulance.md) | 163.9 mph | 4.9 s |
| [Municipal Cruiser 91-A Square](Municipal/Cruiser%2091-A%20Square.md) | 157.5 mph | 4.6 s |
| [Municipal Cruiser 91-B Aero](Municipal/Cruiser%2091-B%20Aero.md) | 165.4 mph | 4.1 s |
| [Municipal Cruiser 91-C Pursuit](Municipal/Cruiser%2091-C%20Pursuit.md) | 171.0 mph | 4.1 s |
| [Municipal Cruiser 91-D Metro](Municipal/Cruiser%2091-D%20Metro.md) | 152.5 mph | 4.1 s |
| [Municipal Cruiser 91-E Highway](Municipal/Cruiser%2091-E%20Highway.md) | 170.7 mph | 4.3 s |
| [Municipal Firetruck](Municipal/Firetruck.md) | 64.2 mph | 4.2 s |
| [Orison Cinder GT](Orison/Cinder%20GT.md) | 179.9 mph | 3.7 s |
| [Pizaz Constant](Pizaz/Constant.md) | 146.1 mph | 4.9 s |
| [Regalia Borough](Regalia/Borough.md) | Not specified — concept only | Not measured — concept only |
| [Rodeo Grazer 4x4](Rodeo/Grazer%204x4.md) | 166.6 mph | 4.2 s |
| [Rodeo Switchback](Rodeo/Switchback.md) | 179.3 mph | 4.9 s |
| [Saddle Tango](Saddle/Tango.md) | 180.8 mph | 4.5 s |
| [Spagatti Shū](Spagatti/Sh%C5%AB.md) | 201.3 mph | 3.7 s |
| [Vesper Mistral](Vesper/Mistral.md) | 173.4 mph | 3.8 s |
| [Vesper Scythe](Vesper/Scythe.md) | 204.7 mph | 3.6 s |
| [Vesper VX-91](Vesper/VX-91.md) | 183.0 mph | 3.9 s |

## Refresh the measurements

From the repository root:

```sh
cmake -S . -B build
cmake --build build --target apricot_vehicle_performance -j 4
build/bin/apricot_vehicle_performance > build/vehicle-performance.csv
```

Review the output before updating the saved CSV, source snapshot, roster and individual model pages together. Keep top speeds in mph and acceleration times to one decimal place. Preserve explicit unmeasured or unsettled states. [Benchmark source](../../../tools/vehicle_performance.cpp).
