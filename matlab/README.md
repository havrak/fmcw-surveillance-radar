# MATLAB directory


== Structure ==
* main app -> singleton
* preferences, platformControl and others are created by main app and aren't singletons


== Ideas ==
* how to do pairing between radar data and platform data
	* we will buffer last 100 positions in platformControl control
	* upon receiving radar data we will estimate when chirp was sent (roughly 2 ms timestamp of the previous data) - than we will get position data from this interval from our buffer
	* output can only be displayed for this position (of course the area will be slightly enlarged due to radiation pattern of the radar)
