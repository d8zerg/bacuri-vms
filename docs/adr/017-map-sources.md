# ADR-017. Map and floor plan sources

[Русская версия](../ru/adr/017-map-sources.md)

**Status** - Accepted.

**Context.** The operator needs a map of the area around the site and detailed plans of the territory and floors. The workplace must open them in an isolated network.

**Decision.** In the web client, use MapLibre GL JS to display OpenStreetMap data. Prepare the package of the required region from a Geofabrik extract. The administrator imports the package to the site, and the browser gets the map and its style resources from the local server. The map shows the region, the date of the source data and the OpenStreetMap attribution with the ODbL license. [Geofabrik regional extracts](https://download.geofabrik.de/) · [OpenStreetMap license and attribution](https://www.openstreetmap.org/copyright)

**Future desktop client.** For Qt, use MapLibre Native for Qt with its C++ API and the Qt Location plugin for QML. The map style must use features supported by both MapLibre GL JS and MapLibre Native. [MapLibre Native for Qt](https://github.com/maplibre/maplibre-native-qt) · [MapLibre style specification](https://maplibre.org/maplibre-style-spec/)

**Common contract.** The site server delivers to both clients over local HTTPS the tiles of the installed region, the style, the fonts and icons of the map, the plan images and the information about the data source. The server stores the identifiers and coordinates of the observation points and returns through the API only the points available to the user. After a point is moved, both clients get its new position at the next read. The map package and the plans are imported once per site.

**Camera orientation.** The camera registry owns the last measurement of the absolute position, the field of view and the connection state, and takes into account the device configuration revision and the ONVIF profile. The maps module gets the measurements of the visible points through a registry operation, stores the calibration separately for each placement and computes the sector angle. A geographic map and a floor plan can have different references. The placement configuration is part of the backup. After recovery, the measurements come from the camera. [ONVIF PTZ service](https://www.onvif.org/specs/srv/ptz/ONVIF-PTZ-Service-Spec.pdf)

**Package delivery.** The Core maps module owns the installed regions, the download queue and the publication of versions. A publisher independently builds a static HTTPS catalog of ready raster MBTiles and can pass the same maps in a ZIP. The administrator chooses the source, the regions and the moment of the update. A stable region key is used for matching, the source data time for choosing an update. The site server keeps the downloaded parts, checks the SHA-256 of an internet package and publishes the replacement of one region in a transaction. The camera coordinates are stored separately. [Region management](../ui.md#installing-and-updating-regions)

**Updates.** Geofabrik usually updates the extracts daily. On the site the changes become visible after a new package is imported. Take the source data date from the extract metadata and keep it at import.

**Site plans.** The customer's master plan is the source for the territory, its floor drawings for the floors. On the geographic map a point position is set by longitude and latitude, on a local plan by coordinates relative to the image. This lets a camera be placed on a drawing without georeferencing and lets live video or the archive of the same observation point be opened from both maps.

**Plan georeferencing.** A plan can remain standalone. The maps module stores two pairs of corresponding landmarks on the plan, or one pair with the image width in meters and the azimuth of its top. The input defines one similarity transformation in local Web Mercator coordinates that takes into account the raster size and the latitude of the landmark. The plan proportions are kept. The reference coordinates belong to the site and survive the replacement of the offline region package. Plans of one building can share a name for floor selection.

**Geoposition source.** An observation point has one position on the area overview. The source is a manual placement or a placement on one of the georeferenced plans. Before a change, the administrator sees the overlay and chooses the source for each point with an existing geoposition. The choice and the georeference are saved atomically after the preview revision is checked. Coordinates and the sector from a plan are computed at read time, taking into account the image rotation and the actual camera status. Removing the georeference keeps the plan, its placements and the original manual coordinates. Access to a plan, its image and its markers is determined by the current permissions for the points. [Georeferencing a plan to the map](../ui.md#georeferencing-a-plan-to-the-map)

**Verification.** With external internet disabled, open the map and a floor plan, choose an available point and go to the recording. After importing a newer package, check the displayed source data date and that the camera bindings are kept. [Region map and plans](../ui.md#region-map-and-plans)

**Package format** - raster regional MBTiles packages with 256 x 256 PNG tiles. The server converts TMS to XYZ, builds the style for MapLibre and serves the attribution. For an internet installation the site server contacts the configured HTTPS catalog. On a closed site the administrator brings the ready maps on media. [Import and client contract](../ui.md#preparing-and-importing-a-region)
