clear
    clc;
    close all;

    %% --------------------------------------------------------------------
    % 1) Define sensors (position in mm, angles in degrees)
    % ---------------------------------------------------------------------
    % Adjust these for your real sensor layout.
    sensors(1) = Sensor(-300, 400, 250, 0, 0, 0); 
    sensors(2) = Sensor(-300, 400, 250, 0, 0, 60); 
    sensors(3) = Sensor(300, 400, 250, 0, 0,  0);  
    sensors(4) = Sensor(300, 400, 250, 0, 0, -60); 
    sensors(5) = Sensor(0, -400, 750, -10, 0, 150); 
    sensors(6) = Sensor(0, -400, 750, -10, 0, -150); 
    sensors(7) = Sensor(0, -400, 750, -10, 0, 90);
    sensors(8) = Sensor(0, -400, 750, -10, 0, -90);

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

    % x, y, z are NxP where P = numSensors*numCells
    % timeSec is Nx1
    
    % clamp Z first
    z(z < 0) = 0;
    
    N = size(x,1);
    P = size(x,2);
    
    xyz = zeros(N, 3*P);
    
    for k = 1:P
        j = (k-1)*3;
        xyz(:, j+1) = x(:, k);
        xyz(:, j+2) = y(:, k);
        xyz(:, j+3) = z(:, k);
    end
    
    M = [timeSec, xyz];

    numSensors = 8;
    numCells   = 16;
    axes       = {'x','y','z'};
    
    headers = {'t_sec'};  % start with time
    
    for s = 1:numSensors
        for c = 1:numCells
            for a = 1:numel(axes)
                headers{end+1} = sprintf('s%dc%d_%s', s, c, axes{a});
            end
        end
    end

    T = array2table(M,"VariableNames",headers);
    writetable(T,'SimOutput_XYZ_Values.csv')
