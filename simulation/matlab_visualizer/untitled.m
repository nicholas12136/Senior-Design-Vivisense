%% WALL TEST FOR getXYZdata
clc; clear;

% ---------------------------------------------------------
% Parameters
% ---------------------------------------------------------
res = 64;                    % 8x8 grid
cellsPerSide = sqrt(res);
L = 2000;                    % distance to flat wall (mm)

% ---------------------------------------------------------
% Build synthetic perfect-wall distances
% d = L / (cos(phi) * cos(theta))
% ---------------------------------------------------------
theta = linspace(-30, 30, cellsPerSide);    % vertical angles
phi   = linspace(-30, 30, cellsPerSide);    % horizontal angles

dGrid = zeros(1, res);
zone = 1;
for r = 1:cellsPerSide
    t = theta(r);
    for c = 1:cellsPerSide
        p = phi(c);
        dGrid(zone) = L ./ (cosd(p) .* cosd(t));
        zone = zone + 1;
    end
end

% Make fake M with 3 identical frames
Mtest = repmat(dGrid, 3, 1);   % [3 x res]

% ---------------------------------------------------------
% Fake sensor object with identity transform
% ---------------------------------------------------------
sensor.getTransform = @() eye(4);
sensors = sensor;  % wrap in array of length 1

% ---------------------------------------------------------
% Call user function
% ---------------------------------------------------------
[x, y, z] = getXYZdata(Mtest, sensors, res);

% ---------------------------------------------------------
% Inspect and verify
% ---------------------------------------------------------
frame = 1;
y_vals = y(frame,:);

fprintf("Expected Y ≈ %f mm\n", L);
fprintf("Min Y = %.3f, Max Y = %.3f\n", min(y_vals), max(y_vals));
fprintf("Std Dev Y = %.6f\n", std(y_vals));

% ---------------------------------------------------------
% Optional: visualize the wall
% ---------------------------------------------------------
figure; scatter3(x(frame,:), y(frame,:), z(frame,:), 50, y(frame,:), 'filled');
xlabel('X'); ylabel('Y'); zlabel('Z'); axis equal; grid on;
title('Synthetic Flat Wall Test');
