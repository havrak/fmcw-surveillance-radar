# MATLAB directory


## Personal notes to polish later

### Structure
* main app -> singleton
* preferences, platformControl and others are created by main app and aren't singletons


### Ideas
* how to do pairing between radar data and platform data
	* we will buffer last 100 positions in platformControl control
	* upon receiving radar data we will estimate when chirp was sent (roughly 2 ms timestamp of the previous data) - than we will get position data from this interval from our buffer
	* output can only be displayed for this position (of course the area will be slightly enlarged due to radiation pattern of the radar)




* radar.m - only manages radar configuration, basic parsing of data into arrays
* radarBuffer.m -
* dataProcesor.m
	* has lisener on radar.m, upon recieving data we grab them and store the in radarBuffer
	* in addition we get list of positions radar was in between oldest and newst item
	* than we kick off parfeval that will process batch, make are recalculations and export it into neat format that can be added to radar cube
	* movement of the platform is taken into consideration at this point
	* back in main thread (but not inside callback in postProcess function we will run CFAR on whole radar cube and do the visualizations
* radarDataCube.m
	* need to check up how they are properly implemented, will store all information about space around the radar


* CFAR - probably could be a different module
* for now RadarCube should be focued mainly on getting azimuth to work (simple range-azimuth map)
* lilting mostion is a bitch


