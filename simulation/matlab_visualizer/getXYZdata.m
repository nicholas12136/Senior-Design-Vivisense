function [x,y,z] = getXYZdata(M, sensors, res)
% getXYZdata  Convert per-cell distance data into world XYZ coordinates.
%
%   [x,y,z] = getXYZdata(M, sensors, res)
%
%   M        : [numFrames x (numSensors*res)] distance matrix (mm)
%   sensors  : array of Sensor objects (each with getTransform())
%   res      : number of cells per sensor (16 or 64, must be a square)
%
%   x,y,z    : [numFrames x (numSensors*res)] world coordinates (mm)

    if nargin < 3
        res = 16;
    end

    numSensors = numel(sensors);
    numRows    = size(M, 1);
    cellsPerSide = sqrt(res);

    if cellsPerSide ~= round(cellsPerSide)
        error('Resolution res must be a perfect square (e.g., 16 or 64).');
    end

    % ---------------------------------------------------------
    % Split distance data into one block per sensor
    % ---------------------------------------------------------
    sensorDistances = cell(1, numSensors);
    start_col = 1;

    for i = 1:numSensors
        end_col = start_col + res - 1;
        sensorDistances{i} = M(:, start_col:end_col);   % [numRows x res]
        start_col = end_col + 1;
    end

    % ---------------------------------------------------------
    % Precompute transforms for each sensor from Sensor class
    % ---------------------------------------------------------
    Transforms = cell(1, numSensors);
    for i = 1:numSensors
        Transforms{i} = sensors(i).getTransform();      % 4x4
    end

    % ---------------------------------------------------------
    % Define FOV angles (deg)
    % theta = vertical angle (down/up), phi = horizontal (left/right)
    % ---------------------------------------------------------
    theta = linspace(-30, 30, cellsPerSide);   % vertical
    phi   = linspace(-30, 30, cellsPerSide);   % horizontal 

    % ---------------------------------------------------------
    % Allocate output arrays
    % ---------------------------------------------------------
    maxPts = numSensors * res;                 % total cells across all sensors
    xWorld = zeros(numRows, maxPts);
    yWorld = zeros(numRows, maxPts);
    zWorld = zeros(numRows, maxPts);

    % ---------------------------------------------------------
    % Main loop: frames → sensors → grid cells
    % ---------------------------------------------------------
    for r = 1:numRows
        idx = 1;  % global point index across all sensors for this frame

        for sIdx = 1:numSensors
            T  = Transforms{sIdx};          % 4x4 sensor→world transform
            sd = sensorDistances{sIdx}(r,:);% 1 x res distances for this frame

            zone = 1;                       % cell index within this sensor

            for row = 1:cellsPerSide
                t = theta(row);             % vertical angle (deg)

                for col = 1:cellsPerSide
                    p = phi(col);           % horizontal angle (deg)
                    d = sd(zone);           % distance for this cell (mm)

                    % Skip invalid / zero distances if desired
                    % if d <= 0
                    %     zone = zone + 1;
                    %     idx  = idx  + 1;
                    %     continue;
                    % end

                    % -----------------------------------------
                    % Local sensor-frame coordinates
                    % +Y forward, +Z up
                    % -----------------------------------------
                    x_loc = -d * sind(p) * cosd(t);   % sideways May need to remove negative sign
                    y_loc = d * cosd(p) * cosd(t);   % forward
                    z_loc = d * sind(t);             % up

                    P1 = [x_loc; y_loc; z_loc; 1];   % homogeneous
                    P0 = T * P1;                     % world coords

                    xWorld(r, idx) = P0(1);
                    yWorld(r, idx) = P0(2);
                    zWorld(r, idx) = P0(3);

                    zone = zone + 1;
                    idx  = idx  + 1;
                end
            end
        end
    end

    x = xWorld;
    y = yWorld;
    z = zWorld;
end
