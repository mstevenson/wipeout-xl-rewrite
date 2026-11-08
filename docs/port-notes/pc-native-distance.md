# Native PC draw distance

Native PC road rendering follows authored section links, with the original F3
cap on north low-detail runs. All-view lists remain uncapped. Precise projection
fills faces outside the compatibility renderer's coordinate range.

Scenery uses the PowerVR candidate-list square root and original integer
skeleton composition, near bound 2048, F3 far bound and projected sphere bounds.
Candidate lists are captured at scene initialization independently of the active
profile. They no longer inherit PlayStation rounding at radius boundaries.

Rendered ships use `AddShipRenderObjects`' 1200/40 limits and float magnitude,
separately from gameplay visibility. Native hulls and shadows share their mask.
Full distance and lockstep tracing retain their existing dispatch.

See [the original function evidence and tests](native-ships.md) and
[the four-profile implementation and verification](native-distance.md).
