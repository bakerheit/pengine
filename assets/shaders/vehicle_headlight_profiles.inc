// Single source for CPU emitter anchors and GPU lens cutouts. Native mesh
// coordinates (+Z nose, +X left); the opposite side is mirrored. Dimensions
// come from the cooked lens faces / painted lens cells, NOT body proportions.
// Keep bezels, amber indicators and gaps between bulbs outside these regions.
HEADLIGHT_MODEL(car5, 0)
HEADLIGHT_RECT(0.61, 0.93, 0.79, 1.08, 3.30, 3.80)
HEADLIGHT_RECT(0.845, 0.93, 1.01, 1.08, 3.30, 3.80)
HEADLIGHT_END
HEADLIGHT_MODEL(car8, 1)
HEADLIGHT_ROUND(0.865, 0.94, 1.085, 1.14, 2.80, 3.20)
HEADLIGHT_END
HEADLIGHT_MODEL(ambulance, 2)
HEADLIGHT_RECT(0.59, 0.69, 0.91, 0.93, 2.99, 3.04)
HEADLIGHT_END
HEADLIGHT_MODEL(halcyon_six, 3)
HEADLIGHT_ROUND(0.69, 0.88, 1.13, 1.32, 3.06, 3.09)
HEADLIGHT_END
HEADLIGHT_MODEL(montrose_regent_eight, 4)
HEADLIGHT_ROUND(0.78, 1.02, 1.32, 1.56, 3.43, 3.46)
HEADLIGHT_END
HEADLIGHT_MODEL(firetruck, 5)
HEADLIGHT_RECT(0.55, 0.78, 1.03, 1.03, 2.99, 3.04)
HEADLIGHT_END
HEADLIGHT_MODEL(glr_lunge, 6)
HEADLIGHT_RECT(0.36, 0.49, 1.04, 0.56, 2.73, 2.77)
HEADLIGHT_END
HEADLIGHT_MODEL(glr_zip, 7)
HEADLIGHT_RECT(0.33, 0.435, 0.58, 0.525, 2.48, 2.52)
HEADLIGHT_RECT(0.64, 0.435, 0.89, 0.525, 2.48, 2.52)
HEADLIGHT_END
HEADLIGHT_MODEL(harrow_workman, 8)
HEADLIGHT_RECT(0.60, 0.78, 0.96, 1.025, 2.44, 2.56)
HEADLIGHT_END
HEADLIGHT_MODEL(alder_wayfarer, 9)
HEADLIGHT_RECT(0.49, 0.66, 0.83, 0.93, 2.55, 2.76)
HEADLIGHT_END
// This source has CLOSED pop-up covers, not exposed lenses. Never turn the
// painted hood into a luminous rectangle. Opening hardware is a separate rig.
HEADLIGHT_MODEL(vesper_vx91, 10)
HEADLIGHT_END
HEADLIGHT_MODEL(alder_ridge, 11)
HEADLIGHT_RECT(0.66, 0.845, 0.91, 1.045, 2.07, 2.09)
HEADLIGHT_END
HEADLIGHT_MODEL(harrow_parcel, 12)
HEADLIGHT_RECT(0.615, 0.86, 0.89, 1.07, 2.53, 2.55)
HEADLIGHT_END
HEADLIGHT_MODEL(vesper_mistral, 13)
HEADLIGHT_RECT(0.44, 0.505, 0.74, 0.57, 2.34, 2.36)
HEADLIGHT_END
HEADLIGHT_MODEL(harrow_cityliner, 15)
HEADLIGHT_RECT(0.76,0.82,1.03,0.96,5.39,5.41)
HEADLIGHT_END

HEADLIGHT_MODEL(alder_pip, 16)
HEADLIGHT_RECT(.48,.59,.68,.70,1.81,1.83)
HEADLIGHT_END

HEADLIGHT_MODEL(vesper_scythe, 17)
HEADLIGHT_RECT(.53,.36,.77,.43,2.235,2.255)
HEADLIGHT_END

HEADLIGHT_MODEL(halcyon_sovereign, 18)
HEADLIGHT_RECT(.59,.73,.84,.85,3.99,4.01)
HEADLIGHT_END

HEADLIGHT_MODEL(harrow_hauler, 19)
HEADLIGHT_RECT(.85,1.25,1.09,1.46,3.02,3.04)
HEADLIGHT_END

// Closed pop-up covers stay dark. The fixed lower fascia lenses supply beams.
HEADLIGHT_MODEL(orison_cinder, 21)
HEADLIGHT_RECT(.38,.30,.62,.415,2.21,2.22)
HEADLIGHT_END
HEADLIGHT_MODEL(spagatti_shu, 22)
// Curved receiver spans the rounded nose; glow reuses its exact body facets.
HEADLIGHT_RECT(.47,.46,.93,.64,1.90,2.405)
HEADLIGHT_END

HEADLIGHT_MODEL(municipal_cruiser_91a, 23)
HEADLIGHT_RECT(.42,.70,.65,.88,2.64,2.675)
HEADLIGHT_RECT(.68,.70,.94,.88,2.64,2.675)
HEADLIGHT_END

HEADLIGHT_MODEL(municipal_cruiser_91b, 24)
HEADLIGHT_RECT(.50,.56,.94,.79,2.58,2.59)
HEADLIGHT_END

HEADLIGHT_MODEL(municipal_cruiser_91c, 25)
HEADLIGHT_RECT(.08,.58,.46,.74,2.64,2.70)
HEADLIGHT_RECT(.50,.58,.91,.74,2.64,2.70)
HEADLIGHT_END

HEADLIGHT_MODEL(municipal_cruiser_91d, 26)
HEADLIGHT_RECT(.46,.70,.68,.88,2.535,2.540)
HEADLIGHT_RECT(.71,.70,.93,.88,2.535,2.540)
HEADLIGHT_END

HEADLIGHT_MODEL(municipal_cruiser_91e, 27)
HEADLIGHT_RECT(.12,.57,.44,.76,2.775,2.780)
HEADLIGHT_RECT(.50,.57,.88,.76,2.775,2.780)
HEADLIGHT_END

HEADLIGHT_MODEL(fang_venom_v2, 28)
// The split endurance lamps are modeled as two narrow inboard receivers.
HEADLIGHT_RECT(.022,.825,.205,.955,1.115,1.130)
HEADLIGHT_END
