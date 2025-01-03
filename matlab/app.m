classdef app < handle 
	properties (Access = public)
		hPreferences
		hPlatformControl 

		hFig 
    hToolbar 
	end

	methods(Access=private)
		function obj = app()
			obj.constructGUI();
			obj.hPreferences = preferences();
			obj.hPlatformControl = platformControl(obj.hPreferences);
		end

		function constructGUI(obj)
			figSize = [800, 600];
			screenSize = get(groot, 'ScreenSize');
			obj.hFig = uifigure('Name', 'FMCW', ...
				'Position', [(screenSize(3:4) - figSize)/2, figSize], ...
				'MenuBar','none', ...
				'NumberTitle', 'off', ...
				'Resize', 'off');
			
			obj.hToolbar = uitoolbar(obj.hFig);

			[img,map] = imread(fullfile(matlabroot,...
				'toolbox','matlab','icons','profiler.gif'));
			ptImage = ind2rgb(img,map);
			
			uipushtool(obj.hToolbar, ...
				'TooltipString', 'Preferences', ...
				'ClickedCallback', @(~,~) obj.hPreferences.showGUI(), ...
				 'CData', ptImage); % Example icon
			
			[img,map] = imread(fullfile(matlabroot,...
				'toolbox','matlab','icons','tool_rotate_3d.gif'));
			ptImage = ind2rgb(img,map);
			uipushtool(obj.hToolbar, ...
				'TooltipString', 'Platform', ...
				'ClickedCallback', @(~,~) obj.hPlatformControl.showGUI(), ...
				 'CData', ptImage); % Example icon
			
		
			[img,map] = imread(fullfile(matlabroot,...
				'toolbox','matlab','icons','helpicon.gif'));
			ptImage = ind2rgb(img,map);
			
			uipushtool(obj.hToolbar, ...
				'TooltipString', 'Help', ...
				'CData', ptImage);

		end
	end

	
	methods(Static=true)

		function handle=getInstance()
			persistent instanceStatic;
			if isempty(instanceStatic) || ~isvalid(instanceStatic.hToolbar)
				fprintf('App | getInstance | Creating new instance\n');
				instanceStatic = app();
			end
			handle=instanceStatic;
		end
	end
end