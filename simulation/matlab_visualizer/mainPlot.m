function mainPlot
% mainPlot
% Vivisense 3D + Top-Down visualization with real-time playback controls
% AND GIF/MP4 recording, using distance-based colormap (no special floor color).
%
% CSV format:
%   col 1: time in ms since recording start
%   col 2..end: distances for all sensors (numSensors * res columns)
%
% Assumes:
%   - Sensor.m
%   - getXYZdata.m
% are on the MATLAB path.
    clear
    clc;
    close all;

    %% --------------------------------------------------------------------
    % 1) Define sensors (position in mm, angles in degrees)
    % ---------------------------------------------------------------------
    % Adjust these for your real sensor layout.
    sensors(1) = Sensor(0, 0, 538, 0, 0, 0); 
    % sensors(2) = Sensor(-500, 500, 250, 0, 0, 60); 
    % sensors(3) = Sensor(500, 500, 250, 0, 0,  0);  
    % sensors(4) = Sensor(500, 500, 250, 0, 0, -60); 
    % sensors(5) = Sensor(0, -500, 500, 0, 0, 150); 
    % sensors(6) = Sensor(0, -500, 500, 0, 0, -150); 

    numSensors = numel(sensors);

    %% --------------------------------------------------------------------
    % 2) Select CSV file and load data (time + distances)
    % ---------------------------------------------------------------------
    [file, path] = uigetfile('*.csv', 'Select a CSV distance file');
    if isequal(file, 0)
        disp('User canceled file selection.');
        return;
    end

    csvFile = fullfile(path, file);
    fprintf('Loading: %s\n', csvFile);

    raw = table2array(readtable(csvFile));   % [numFrames x (1 + numSensors*res)]

    timeMs = raw(:,1);                       % first column = time (ms since start)
    data   = raw(:,2:end);                   % distance columns only

    % --- Auto-detect resolution (must be 16 or 64) ---
    numDistCols = size(data, 2);
    res = numDistCols / numSensors;
    if res ~= 16 && res ~= 64
        error('Automatic resolution detect failed: got %.0f per sensor, expected 16 or 64.', res);
    end
    fprintf('Detected resolution: %.0f (grid %dx%d)\n', res, sqrt(res), sqrt(res));

    %% --------------------------------------------------------------------
    % 3) Convert distances → world coordinates
    % ---------------------------------------------------------------------
    [x, y, z] = getXYZdata(data, sensors, res);

    assignin("base","x",x);
    assignin("base","y",y);
    assignin("base","z",z);

    % Clamp any negative Z to 0 (floor)
    z(z < 0) = 0;

    numFrames = size(x, 1);

    % Time vector in seconds starting at 0
    timeSec = (timeMs - timeMs(1)) / 1000;

    %% --------------------------------------------------------------------
    % 4) Prepare colormap and initial distance
    % ---------------------------------------------------------------------
    d = sqrt(x(1,:).^2 + y(1,:).^2);   % distance in XY for coloring

    myMap = [
        1 0 0;   % red   (near)
        1 1 0;   % yellow
        0 1 0;   % green (far)
    ];

    % State variables (visible to nested functions)
    currentFrame = 1;
    isPlaying    = false;

    % --- NEW: distance / floor display settings ---
    distThresh           = 2500;   % mm, points farther than this can be hidden
    showOnlyBelowThresh  = false;   % set false to show all distances
    floorTol             = 100;     % mm, |z| <= floorTol is treated as floor


    %% --------------------------------------------------------------------
    % 5) Figure and axes setup
    % ---------------------------------------------------------------------
    fig = figure("Name","3D + Top-Down Player");

    ax3 = subplot(1,2,1);   % 3D view
    ax2 = subplot(1,2,2);   % top-down view

    drawnow;
    % Move axes up to leave space for buttons at bottom
    ax3.Position = [0.05 0.20 0.60 0.72];  % [left bottom width height]
    ax2.Position = [0.70 0.20 0.25 0.72];

    %% ------------------------ 3D VIEW SETUP -----------------------------
    axes(ax3); %#ok<LAXES>
    hold(ax3, "on");

    % Origin axes
    axisLength = 300;  % axis arrow length in mm
    % 
    % quiver3(ax3, 0,0,0, axisLength,0,0, ...
    %     'Color',[1 0 0], 'LineWidth',2, 'MaxHeadSize',0.5);  % X (red)
    % quiver3(ax3, 0,0,0, 0,axisLength,0, ...
    %     'Color',[0 1 0], 'LineWidth',2, 'MaxHeadSize',0.5);  % Y (green)
    % quiver3(ax3, 0,0,0, 0,0,axisLength, ...
    %     'Color',[0 0 1], 'LineWidth',2, 'MaxHeadSize',0.5);  % Z (blue)

    text(axisLength, 0, 0, 'X', 'FontSize',12, 'Color',[1 0 0]);
    text(0, axisLength, 0, 'Y', 'FontSize',12, 'Color',[0 1 0]);
    text(0, 0, axisLength, 'Z', 'FontSize',12, 'Color',[0 0 1]);

    % Forward reference line in +Y
    plot3(ax3, zeros(10), linspace(0,3000,10), zeros(10), ...
          '--', 'Color', [0 1 0]);

    % Forward reference line in +Y
    plot3(ax3, linspace(0,3000,10), zeros(10), zeros(10), ...
          '--', 'Color', [1 0 0]);

    %Plot sensors
    for s = 1:length(sensors)
        scatter3(ax3, sensors(s).x, sensors(s).y, sensors(s).z, 50, 'c', 'cyan');
    end

    scatter3(ax3, 0, 0, 0, 50, 'c', 'filled', 'MarkerFaceColor', 'magenta', 'MarkerEdgeColor', 'magenta');


        colormap(ax3, myMap);
    clim(ax3, [0 3500]);
    colorbar(ax3);

    % --- NEW: initial 3D scatter with floor vs non-floor + threshold ---
    dFrame1 = data(1,:);          % raw distances for frame 1
    zFrame1 = z(1,:);

    % Threshold mask (what's allowed to be visible)
    if showOnlyBelowThresh
        maskThresh1 = dFrame1 <= distThresh;
    else
        maskThresh1 = true(size(dFrame1));
    end

    % Floor vs non-floor
    isFloor1      = abs(zFrame1) <= floorTol;
    isNonFloor1   = ~isFloor1;

    maskNF1    = maskThresh1 & isNonFloor1;  % non-floor points we keep
    maskFloor1 = maskThresh1 & isFloor1;     % floor points we keep

    % Non-floor points: distance-colored
    h3 = scatter3(ax3, ...
        x(1,maskNF1), y(1,maskNF1), z(1,maskNF1), ...
        25, dFrame1(maskNF1), 'filled');

    % Floor points: fixed blue color
    h3Floor = scatter3(ax3, ...
        x(1,maskFloor1), y(1,maskFloor1), z(1,maskFloor1), ...
        30, 'b', 'filled');


    xlim(ax3, [-4000 4000]);
    ylim(ax3, [-4000 4000]);
    zlim(ax3, [0 4000]);
    view(ax3, 3);
    grid(ax3, "on");
    xlabel(ax3, "X (mm)");
    ylabel(ax3, "Y (mm)");
    zlabel(ax3, "Z (mm)");
    title(ax3, "3D View");

    %% --------------------- TOP-DOWN VIEW SETUP --------------------------
       
    axes(ax2); %#ok<LAXES>
    hold(ax2, "on");

    % --- NEW: initial 2D scatter with floor vs non-floor + threshold ---
    h2 = scatter(ax2, ...
        x(1,maskNF1), y(1,maskNF1), ...
        25, dFrame1(maskNF1), 'filled');

    h2Floor = scatter(ax2, ...
        x(1,maskFloor1), y(1,maskFloor1), ...
        30, 'b', 'filled');

    % Origin marker (wheelchair)
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

    %% --------------------------------------------------------------------
    % 6) UI controls: time display + playback + recording buttons
    % ---------------------------------------------------------------------
    % Time / frame label (top center)
    timeText = uicontrol('Style','text', ...
                         'Units','normalized', ...
                         'Position',[0.35 0.93 0.30 0.04], ...
                         'String','t = 0.000 s (frame 1/1)', ...
                         'FontSize',11, ...
                         'BackgroundColor', get(fig,'Color'));

    % Bottom button bar
    btnY  = 0.06;
    btnH  = 0.06;
    btnW  = 0.08;

    % Step back
    uicontrol('Style','pushbutton', ...
              'String','<<', ...
              'Units','normalized', ...
              'Position',[0.20 btnY btnW btnH], ...
              'Callback', @onStepBack);

    % Play/Pause
    playBtn = uicontrol('Style','pushbutton', ...
                        'String','Play', ...
                        'Units','normalized', ...
                        'Position',[0.32 btnY btnW btnH], ...
                        'Callback', @onPlayPause);

    % Step forward
    uicontrol('Style','pushbutton', ...
              'String','>>', ...
              'Units','normalized', ...
              'Position',[0.44 btnY btnW btnH], ...
              'Callback', @onStepForward);

    % Save GIF
    uicontrol('Style','pushbutton', ...
              'String','Save GIF', ...
              'Units','normalized', ...
              'Position',[0.60 btnY btnW btnH], ...
              'Callback', @(src,evt) recordAnimation('gif'));

    % Save MP4
    uicontrol('Style','pushbutton', ...
              'String','Save MP4', ...
              'Units','normalized', ...
              'Position',[0.72 btnY btnW btnH], ...
              'Callback', @(src,evt) recordAnimation('mp4'));

    % Initial frame update
    updateFrame(1);

    %% ====================================================================
    % Nested helper: updateFrame
    % ====================================================================
    function updateFrame(newFrame)
        % Clamp frame index
        newFrame = max(1, min(numFrames, newFrame));
        currentFrame = newFrame;

        % Raw distances and coordinates for this frame
        dFrame = data(currentFrame,:);
        xF     = x(currentFrame,:);
        yF     = y(currentFrame,:);
        zF     = z(currentFrame,:);

        % --- Distance threshold mask ---
        if showOnlyBelowThresh
            maskThresh = dFrame <= distThresh;
        else
            maskThresh = true(size(dFrame));
        end

        % --- Floor vs non-floor based on z ---
        isFloorAll    = abs(zF) <= floorTol;
        isNonFloorAll = ~isFloorAll;

        maskNF    = maskThresh & isNonFloorAll;  % normal points
        maskFloor = maskThresh & isFloorAll;     % floor points

        % ==== 3D view ====
        % Non-floor points: colored by distance
        h3.XData = xF(maskNF);
        h3.YData = yF(maskNF);
        h3.ZData = zF(maskNF);
        h3.CData = dFrame(maskNF);

        % Floor points: fixed blue
        h3Floor.XData = xF(maskFloor);
        h3Floor.YData = yF(maskFloor);
        h3Floor.ZData = zF(maskFloor);

        % ==== Top-down view ====
        h2.XData = xF(maskNF);
        h2.YData = yF(maskNF);
        h2.CData = dFrame(maskNF);

        h2Floor.XData = xF(maskFloor);
        h2Floor.YData = yF(maskFloor);

        % Update time label (existing handle)
        if isgraphics(timeText)
            timeText.String = sprintf('t = %.3f s (frame %d/%d)', ...
                                      timeSec(currentFrame), ...
                                      currentFrame, numFrames);
        end

        drawnow limitrate;
end


    %% ====================================================================
    % Nested callback: Play/Pause (real-time using timestamps)
    % ====================================================================
    function onPlayPause(src, ~)
        if ~isPlaying
            % If we're on the last frame, restart from frame 1
            if currentFrame >= numFrames
                updateFrame(1);
            end

            % Start playing
            isPlaying = true;
            if ishandle(src)
                set(src, 'String', 'Pause');
            end

            % Real-time loop using timestamps
            while isPlaying && currentFrame < numFrames && ishandle(fig)
                nextFrame = currentFrame + 1;
                dt = timeSec(nextFrame) - timeSec(currentFrame);
                if dt < 0
                    dt = 0;
                end

                pause(dt);

                if ~isPlaying || ~ishandle(fig)
                    break;
                end

                updateFrame(nextFrame);
            end

            % Stop playing
            isPlaying = false;
            if ishandle(src)
                set(src, 'String', 'Play');
            end
        else
            % Currently playing → pause
            isPlaying = false;
            if ishandle(src)
                set(src, 'String', 'Play');
            end
        end
    end

    %% ====================================================================
    % Nested callback: Step forward one frame
    % ====================================================================
    function onStepForward(~, ~)
        isPlaying = false;
        if ishandle(playBtn)
            set(playBtn, 'String', 'Play');
        end
        updateFrame(currentFrame + 1);
    end

    %% ====================================================================
    % Nested callback: Step back one frame
    % ====================================================================
    function onStepBack(~, ~)
        isPlaying = false;
        if ishandle(playBtn)
            set(playBtn, 'String', 'Play');
        end
        updateFrame(currentFrame - 1);
    end

    %% ====================================================================
    % Nested helper: recordAnimation (GIF/MP4)
    % ====================================================================
    function recordAnimation(mode)
        % Stop playback if running
        isPlaying = false;
        if ishandle(playBtn)
            set(playBtn, 'String', 'Play');
        end

        mode = lower(string(mode));
        makeGif = (mode == "gif");
        makeMp4 = (mode == "mp4");

        if ~makeGif && ~makeMp4
            return;
        end

        % Ask for filename
        if makeGif
            [gFile, gPath] = uiputfile('*.gif', 'Save animation as GIF');
            if isequal(gFile,0)
                disp('GIF save canceled.');
                return;
            end
            gifFile = fullfile(gPath, gFile);
            fprintf('Saving GIF to: %s\n', gifFile);
        end

        if makeMp4
            [vFile, vPath] = uiputfile('*.mp4', 'Save animation as MP4');
            if isequal(vFile,0)
                disp('MP4 save canceled.');
                return;
            end
            vidFile = fullfile(vPath, vFile);
            fprintf('Saving MP4 to: %s\n', vidFile);
            v = VideoWriter(vidFile, 'MPEG-4');
            v.FrameRate = 15;  % fixed playback rate in file
            open(v);
        end

        frameDelay = 1 / 15;   % for GIF playback timing
        figHandle = fig;       % capture current figure

        % Loop through all frames for recording
        % Loop through all frames for recording
        for k = 1:numFrames
            % Use the same logic as live playback (includes thresholds & floor)
            updateFrame(k);
        
            % Make sure the graphics are updated before capture
            drawnow;
        
            % Capture frame
            F = getframe(figHandle);
        
            % Write GIF
            if makeGif
                [im, map] = rgb2ind(frame2im(F), 256);
                if k == 1
                    imwrite(im, map, gifFile, 'gif', ...
                            'LoopCount', Inf, 'DelayTime', frameDelay);
                else
                    imwrite(im, map, gifFile, 'gif', ...
                            'WriteMode', 'append', 'DelayTime', frameDelay);
                end
            end
        
            % Write MP4
            if makeMp4
                writeVideo(v, F);
            end
        end


        if makeMp4
            close(v);
            disp('MP4 save complete.');
        end
        if makeGif
            disp('GIF save complete.');
        end

        % After recording, reset display to last frame
        updateFrame(numFrames);
    end

end
