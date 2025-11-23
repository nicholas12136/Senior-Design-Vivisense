% mainPlot.m
% Vivisense 3D + Top-Down Point Cloud Visualization with replay and export

clc;
clear;
close all;

%% ------------------------------------------------------------------------
% 1) Add project paths (edit/remove if needed)
% -------------------------------------------------------------------------
% If Sensor.m and getXYZdata.m are in a different folder than this script,
% uncomment and adjust:
%
% addpath('matlab');
% addpath('matlab/utils');

%% ------------------------------------------------------------------------
% 2) Define sensors (position in mm, angles in degrees)
% -------------------------------------------------------------------------
sensors(1) = Sensor(0, 0, 500, 0, 0, 30); % Front Left Forward
sensors(2) = Sensor(0, 0, 500, 0, 0,  90);  % Front Left Corner
sensors(3) = Sensor(0, 0, 500, 0, 0, -30); %Front Right Forward
sensors(4) = Sensor(0, 0, 500, 0, 0, -90); %Front Left Corner
sensors(5) = Sensor(0, 0, 500, 0, 0, 150); %Back Left 
sensors(6) = Sensor(0, 0, 500, 0, 0, -150); %Back Right

res        = 16;              % cells per sensor (16 = 4x4, 64 = 8x8)
numSensors = numel(sensors);

%% ------------------------------------------------------------------------
% 3) Select CSV file and load data
% -------------------------------------------------------------------------
[file, path] = uigetfile('*.csv', 'Select a CSV distance file');
if isequal(file, 0)
    disp('User canceled file selection.');
    return;
end

csvFile = fullfile(path, file);
fprintf('Loading: %s\n', csvFile);

data = table2array(readtable(csvFile));  % [numFrames x (numSensors*res)]

% Sanity check on column count
expectedCols = numSensors * res;
if size(data, 2) ~= expectedCols
    error('CSV has %d columns, but expected %d (= %d sensors * %d cells).', ...
        size(data,2), expectedCols, numSensors, res);
end

%% ------------------------------------------------------------------------
% 4) Convert distances → world coordinates
% -------------------------------------------------------------------------
[x, y, z] = getXYZdata(data, sensors, res);

% Optional: clamp negative Z to 0 (floor)
z(z < 0) = 0;

% Number of frames (one row per frame)
numFrames = size(x, 1);

%% ------------------------------------------------------------------------
% 5) Prepare distance-based coloring (frame 1)
% -------------------------------------------------------------------------
d = sqrt(x(1,:).^2 + y(1,:).^2);   % distance in XY plane from origin

% Custom colormap: red → yellow → green
myMap = [
    1 0 0;   % red   (near)
    1 1 0;   % yellow
    0 1 0;   % green (far)
];

%% ------------------------------------------------------------------------
% 6) Create figure with 3D and top-down views (using subplot)
% -------------------------------------------------------------------------
fig = figure("Name","3D + Top-Down View","Color",'black');

% 3D view (left) and top-down view (right)
ax3 = subplot(1,2,1);   % 3D view
ax2 = subplot(1,2,2);   % Top-down

% Adjust axes positions so 3D view is wider
drawnow;  % let MATLAB assign default positions first
ax3.Position = [0.05 0.10 0.60 0.80];   % [left bottom width height]
ax2.Position = [0.70 0.10 0.25 0.80];

%% ------------------------ 3D VIEW SETUP ---------------------------------
axes(ax3); %#ok<LAXES>
hold(ax3, "on");

% --- Draw origin coordinate axes instead of red dot ---
axisLength = 300;  % length of axis arrows in mm

% X-axis (red)
quiver3(ax3, 0,0,0, axisLength,0,0, ...
    'Color',[1 0 0], 'LineWidth',2, 'MaxHeadSize',0.5);

% Y-axis (green)
quiver3(ax3, 0,0,0, 0,axisLength,0, ...
    'Color',[0 1 0], 'LineWidth',2, 'MaxHeadSize',0.5);

% Z-axis (blue)
quiver3(ax3, 0,0,0, 0,0,axisLength, ...
    'Color',[0 0 1], 'LineWidth',2, 'MaxHeadSize',0.5);

% Axis labels
text(axisLength, 0, 0, 'X', 'FontSize',12, 'Color',[1 0 0]);
text(0, axisLength, 0, 'Y', 'FontSize',12, 'Color',[0 1 0]);
text(0, 0, axisLength, 'Z', 'FontSize',12, 'Color',[0 0 1]);

% Reference line straight ahead in +Y
plot3(ax3, zeros(10), linspace(0,3000,10), zeros(10), ...
      '--', 'Color', [0 1 0]);

colormap(ax3, myMap);
clim(ax3, [0 2000]);      % color scale based on distance (mm)
colorbar(ax3);

% Initial 3D point cloud (frame 1)
h3 = scatter3(ax3, x(1,:), y(1,:), z(1,:), 25, d, 'filled');

xlim(ax3, [-3000 3000]);
ylim(ax3, [-3000 3000]);
zlim(ax3, [0 3000]);
view(ax3, 3);
grid(ax3, "on");
xlabel(ax3, "X (mm)");
ylabel(ax3, "Y (mm)");
zlabel(ax3, "Z (mm)");
title(ax3, "3D View");

%% --------------------- TOP-DOWN VIEW SETUP ------------------------------
axes(ax2); %#ok<LAXES>
hold(ax2, "on");

% Top-down uses X-Y only (Z ignored)
h2 = scatter(ax2, x(1,:), y(1,:), 25, d, 'filled');

% Origin marker (wheelchair location) in top-down view
scatter(ax2, 0, 0, 70, 'red', 'filled');

colormap(ax2, myMap);
clim(ax2, [0 2000]);
colorbar(ax2);

axis(ax2, 'equal');
grid(ax2, 'on');
xlim(ax2, [-3000 3000]);
ylim(ax2, [-3000 3000]);
xlabel(ax2, "X (mm)");
ylabel(ax2, "Y (mm)");
title(ax2, "Top-Down View (X-Y)");

%% ------------------------------------------------------------------------
% 7) Add UI buttons for replay and saving
% -------------------------------------------------------------------------
% Replay button
uicontrol('Style','pushbutton', ...
          'String','Replay', ...
          'Units','normalized', ...
          'Position',[0.40 0.93 0.08 0.05], ...
          'Callback', @(src,evt) runAnimation(ax3, ax2, h3, h2, x, y, z, 'none'));

% Save GIF button
uicontrol('Style','pushbutton', ...
          'String','Save GIF', ...
          'Units','normalized', ...
          'Position',[0.50 0.93 0.08 0.05], ...
          'Callback', @(src,evt) runAnimation(ax3, ax2, h3, h2, x, y, z, 'gif'));

% Save MP4 button
uicontrol('Style','pushbutton', ...
          'String','Save MP4', ...
          'Units','normalized', ...
          'Position',[0.60 0.93 0.08 0.05], ...
          'Callback', @(src,evt) runAnimation(ax3, ax2, h3, h2, x, y, z, 'mp4'));

%% ------------------------------------------------------------------------
% 8) Run animation once initially
% -------------------------------------------------------------------------
runAnimation(ax3, ax2, h3, h2, x, y, z, 'none');


%% ========================================================================
% Local function: runAnimation
% ========================================================================
function runAnimation(ax3, ax2, h3, h2, x, y, z, recordMode)
% runAnimation  Replay the point cloud animation and optionally record it.
%
%   recordMode:
%       'none' - just animate on screen
%       'gif'  - ask for file and save as animated GIF
%       'mp4'  - ask for file and save as MP4 video

    numFrames  = size(x, 1);
    frameDelay = 0.05;   % seconds per frame (for on-screen and GIF timing)

    fig = ancestor(ax3, 'figure');

    % Determine recording mode
    recordMode = lower(string(recordMode));
    makeGif = (recordMode == "gif");
    makeMp4 = (recordMode == "mp4");

    gifFile = '';
    v = [];  % VideoWriter handle

    % --- Set up GIF file if needed ---
    if makeGif
        [gFile, gPath] = uiputfile('*.gif', 'Save animation as GIF');
        if isequal(gFile, 0)
            disp('GIF save canceled. Playing without recording.');
            makeGif = false;
        else
            gifFile = fullfile(gPath, gFile);
            fprintf('Saving GIF to: %s\n', gifFile);
        end
    end

    % --- Set up MP4 file if needed ---
    if makeMp4
        [vFile, vPath] = uiputfile('*.mp4', 'Save animation as MP4');
        if isequal(vFile, 0)
            disp('MP4 save canceled. Playing without recording.');
            makeMp4 = false;
        else
            vidFile = fullfile(vPath, vFile);
            fprintf('Saving MP4 to: %s\n', vidFile);
            v = VideoWriter(vidFile, 'MPEG-4');
            v.FrameRate = 1 / frameDelay;
            open(v);
        end
    end

    % --- Animation loop ---
    for frame = 1:numFrames
        d = sqrt(x(frame,:).^2 + y(frame,:).^2);

        % Update 3D view
        h3.XData = x(frame,:);
        h3.YData = y(frame,:);
        h3.ZData = z(frame,:);
        h3.CData = d;

        % Update top-down view (h2 is the point cloud scatter on ax2)
        h2.XData = x(frame,:);
        h2.YData = y(frame,:);
        h2.CData = d;

        drawnow;

        % Capture frame if recording
        if makeGif || makeMp4
            F = getframe(fig);
        end

        if makeGif
            [im, map] = rgb2ind(frame2im(F), 256);
            if frame == 1
                imwrite(im, map, gifFile, 'gif', ...
                        'LoopCount', Inf, 'DelayTime', frameDelay);
            else
                imwrite(im, map, gifFile, 'gif', ...
                        'WriteMode', 'append', 'DelayTime', frameDelay);
            end
        end

        if makeMp4
            writeVideo(v, F);
        end

        %Only pause for on-screen smoothness if NOT recording video
        if ~makeMp4
            pause(frameDelay);
        end
    end

    % Close video writer if used
    if makeMp4 && ~isempty(v)
        close(v);
        disp('MP4 save complete.');
    end

    if makeGif
        disp('GIF save complete.');
    end
end
