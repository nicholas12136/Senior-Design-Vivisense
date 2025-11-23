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

    clc;
    close all;

    %% --------------------------------------------------------------------
    % 1) Define sensors (position in mm, angles in degrees)
    % ---------------------------------------------------------------------
    % Adjust these for your real sensor layout.
    sensors(1) = Sensor(0, 0, 500, 0, 0, 30); % Front Left Forward
    sensors(2) = Sensor(0, 0, 500, 0, 0,  -30);  % Front Right Forward
    sensors(3) = Sensor(0, 0, 500, 0, 0, -90); %Front Left Corner
    sensors(4) = Sensor(0, 0, 500, 0, 0, 150); %Back Left 
    sensors(5) = Sensor(0, 0, 500, 0, 0, -150); %Back Right
    sensors(6) = Sensor(0, 0, 500, 0, 0, 90); %Front Right Forward

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

    quiver3(ax3, 0,0,0, axisLength,0,0, ...
        'Color',[1 0 0], 'LineWidth',2, 'MaxHeadSize',0.5);  % X (red)
    quiver3(ax3, 0,0,0, 0,axisLength,0, ...
        'Color',[0 1 0], 'LineWidth',2, 'MaxHeadSize',0.5);  % Y (green)
    quiver3(ax3, 0,0,0, 0,0,axisLength, ...
        'Color',[0 0 1], 'LineWidth',2, 'MaxHeadSize',0.5);  % Z (blue)

    text(axisLength, 0, 0, 'X', 'FontSize',12, 'Color',[1 0 0]);
    text(0, axisLength, 0, 'Y', 'FontSize',12, 'Color',[0 1 0]);
    text(0, 0, axisLength, 'Z', 'FontSize',12, 'Color',[0 0 1]);

    % Forward reference line in +Y
    plot3(ax3, zeros(10), linspace(0,3000,10), zeros(10), ...
          '--', 'Color', [0 1 0]);

    colormap(ax3, myMap);
    clim(ax3, [0 2000]);
    colorbar(ax3);

    % Initial 3D scatter (frame 1, colored by distance)
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

    %% --------------------- TOP-DOWN VIEW SETUP --------------------------
    axes(ax2); %#ok<LAXES>
    hold(ax2, "on");

    % Initial 2D scatter (top-down)
    h2 = scatter(ax2, x(1,:), y(1,:), 25, d, 'filled');

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

        % Distance for coloring
        dLocal = sqrt(x(currentFrame,:).^2 + y(currentFrame,:).^2);

        % Update 3D scatter
        h3.XData = x(currentFrame,:);
        h3.YData = y(currentFrame,:);
        h3.ZData = z(currentFrame,:);
        h3.CData = dLocal;

        % Update top-down scatter
        h2.XData = x(currentFrame,:);
        h2.YData = y(currentFrame,:);
        h2.CData = dLocal;

        % Update time label
        tNow = timeSec(currentFrame);
        set(timeText, 'String', ...
            sprintf('t = %.3f s (frame %d/%d)', tNow, currentFrame, numFrames));

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
        for k = 1:numFrames
            % Distance for coloring
            dLocal = sqrt(x(k,:).^2 + y(k,:).^2);

            % Update 3D scatter
            set(h3, 'XData', x(k,:), ...
                    'YData', y(k,:), ...
                    'ZData', z(k,:), ...
                    'CData', dLocal);

            % Update top-down scatter
            set(h2, 'XData', x(k,:), ...
                    'YData', y(k,:), ...
                    'CData', dLocal);

            % Update time label
            tNow = timeSec(k);
            set(timeText, 'String', ...
                sprintf('t = %.3f s (frame %d/%d)', tNow, k, numFrames));

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
