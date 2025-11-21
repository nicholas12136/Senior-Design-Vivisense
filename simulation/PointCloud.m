% ==============================================================================
% SCRIPT: Vivisense 
% AUTHOR: [Nick G, Damon F, Sahil S]
% DATE:   2025-11-19
%
% DESCRIPTION:
% This script takes raw CSV data from 6 VL53L5CX ToF sensors, transforms the
% coordinates from the "Sensor Frame" to the "Global Hallway Frame", and 
% visualizes the result as a color-coded 3D Point Cloud.
%
% COLOR CODE LEGEND:
% - RED:    Immediate Danger (< 0.5m)
% - YELLOW: Warning (< 1.0m)
% - GREEN:  Safe (> 1.0m)
% ==============================================================================

% ==============================================================================
% SCRIPT: Wheelchair Obstacle Detection - Robot Centric Player
% ==============================================================================

function Wheelchair_Player_v10()
    % V10 UPDATES:
    % - Realistic Visibility Radius (4.0m) - Hardware Accurate.
    % - Diverse Obstacle Types: Pillar, Low Hazard, Doorway Squeeze.
    % - Walls are still at +/- 8m (16m wide), but effectively invisible now.
    
    clearvars -except func_handles; clc; close all;

    %% --- SECTION 1: CONFIGURATION ---
    
    SIMULATION_MODE = true; 
    csv_filename = 'hallway_scan_test.csv'; 
    
    % --- VISUALS ---
    % VL53L5CX realistic limit. Walls at 8m will be invisible!
    VISIBILITY_RADIUS = 4.0; 
    
    % Coloring 
    DIST_DANGER  = 1.0; 
    DIST_WARNING = 2.0; 
    
    % --- VIRTUAL ENVIRONMENT ---
    HALLWAY_WIDTH = 16.0; % Walls are at +/- 8m
    
    % Obstacles: [Center_X, Center_Y, Width_X, Width_Y, Height_Z]
    OBSTACLES = [
        % 1. The "Pillar" (Tall, Thin, Left Side)
        3.0,  1.5,  0.3, 0.3, 2.0; 
        
        % 2. The "Trip Hazard" (Low box in middle - tests angled sensors)
        6.5,  0.0,  0.5, 0.5, 0.3;
        
        % 3. The "Doorway Squeeze" (Two boxes creating a gap)
        11.0,  2.0, 1.0, 1.5, 2.0; % Left side of door
        11.0, -2.0, 1.0, 1.5, 2.0; % Right side of door
        
        % 4. The "Wall Jut" (Right side)
        14.0, -1.5, 0.5, 3.0, 2.0;
    ];
    
    % --- ROBOT & SENSOR SETUP ---
    velocity = 0.6; 
    
    sensors = struct();
    
    % --- FRONT PODS ---
    sensors(1).off = [ 0.4,  0.3, 0.5]; sensors(1).yaw = 0;    % Front Left
    sensors(2).off = [ 0.4,  0.3, 0.5]; sensors(2).yaw = 45;   % Front Left Angled
    sensors(3).off = [ 0.4, -0.3, 0.5]; sensors(3).yaw = 0;    % Front Right
    sensors(4).off = [ 0.4, -0.3, 0.5]; sensors(4).yaw = -45;  % Front Right Angled
    
    % --- REAR SENSORS ---
    sensors(5).off = [-0.3,  0.2, 0.9]; sensors(5).yaw = 135;  
    sensors(6).off = [-0.3, -0.2, 0.9]; sensors(6).yaw = -135; 

    %% --- SECTION 2: DATA GENERATION ---
    
    if SIMULATION_MODE
        disp('Running Obstacle Course Simulation (dt=0.20s)...');
        raw_data = generate_realistic_data(sensors, OBSTACLES, HALLWAY_WIDTH, velocity);
    else
        raw_data = readmatrix(csv_filename);
    end

    timestamps = unique(raw_data(:,1));
    total_steps = length(timestamps);
    
    all_frames = struct('Location', {}, 'RobotX', {}, 'Time', {});
    
    disp('Pre-calculating Visualization Frames...');
    
    cumulative_pts = [];

    for t_idx = 1:total_steps
        t = timestamps(t_idx);
        robot_x = velocity * t;
        
        current_rows = raw_data(raw_data(:,1) == t, :);
        
        new_pts = [];
        
        if ~isempty(current_rows)
            for k = 1:height(current_rows)
                s_id = current_rows(k, 2);
                local_pt = current_rows(k, 3:5);
                
                if norm(local_pt) == 0, continue; end
                
                % 1. ROTATE
                yaw_rad = deg2rad(sensors(s_id).yaw);
                R = [cos(yaw_rad) -sin(yaw_rad) 0; sin(yaw_rad) cos(yaw_rad) 0; 0 0 1];
                rotated_pt = (R * local_pt')';
                
                % 2. TRANSLATE to Chair Frame
                pt_chair = rotated_pt + sensors(s_id).off;
                
                % 3. TRANSLATE to Global Frame
                pt_global = pt_chair;
                pt_global(1) = pt_global(1) + robot_x;
                
                new_pts = [new_pts; pt_global];
            end
        end
        
        cumulative_pts = [cumulative_pts; new_pts];
        all_frames(t_idx).Location = cumulative_pts;
        all_frames(t_idx).RobotX   = robot_x;
        all_frames(t_idx).Time     = t;
    end

    %% --- SECTION 3: GUI ---
    current_step = 1; is_playing = false; 
    hFig = figure('Name', 'Wheelchair Simulation V10', 'Position', [100, 100, 1000, 600], 'Color', [0.94,0.94,0.94]);
    ax = axes('Parent', hFig, 'Position', [0.05, 0.25, 0.9, 0.7]);
    
    uicontrol('Parent', hFig, 'Style', 'pushbutton', 'String', '< PREV', ...
              'Units', 'normalized', 'Position', [0.2, 0.05, 0.1, 0.1], 'Callback', @cb_prev);
    btn_play = uicontrol('Parent', hFig, 'Style', 'pushbutton', 'String', 'PLAY SEQUENCE', ...
                         'Units', 'normalized', 'Position', [0.32, 0.05, 0.2, 0.1], ...
                         'Callback', @cb_play_toggle, 'BackgroundColor', [0.6, 1.0, 0.6], 'FontSize', 11, 'FontWeight', 'bold');
    uicontrol('Parent', hFig, 'Style', 'pushbutton', 'String', 'NEXT >', ...
              'Units', 'normalized', 'Position', [0.54, 0.05, 0.1, 0.1], 'Callback', @cb_next);
    lbl_time = uicontrol('Parent', hFig, 'Style', 'text', 'String', 'T = 0.00 s', ...
                         'Units', 'normalized', 'Position', [0.66, 0.07, 0.15, 0.06], ...
                         'BackgroundColor', [0,0,0], 'ForegroundColor', [0,1,1], 'FontSize', 14, 'FontName', 'Consolas');

    update_plot(); 

    %% --- CALLBACKS ---
    function cb_prev(~,~), if ~is_playing && current_step>1, current_step=current_step-1; update_plot(); end, end
    function cb_next(~,~), if ~is_playing && current_step<total_steps, current_step=current_step+1; update_plot(); end, end
    function cb_play_toggle(~, ~)
        if is_playing, is_playing = false; btn_play.String='RESUME'; btn_play.BackgroundColor=[0.6,1,0.6];
        else, is_playing=true; btn_play.String='STOP'; btn_play.BackgroundColor=[1,0.4,0.4];
            while is_playing && current_step<total_steps, current_step=current_step+1; update_plot(); drawnow; pause(0.05); end
            if current_step==total_steps, is_playing=false; btn_play.String='RESTART'; btn_play.BackgroundColor=[0.6,1,0.6]; current_step=1; end
        end
    end

    function update_plot()
        pts = all_frames(current_step).Location;
        robot_x_now = all_frames(current_step).RobotX;
        lbl_time.String = sprintf('T = %.2f s', all_frames(current_step).Time);
        
        cla(ax); hold(ax, 'on');
        
        plot3(ax, 0, 0, 0, 'ro', 'MarkerSize', 12, 'MarkerFaceColor', 'r');
        
        for s_idx = 1:6
            s_pos = sensors(s_idx).off;
            plot3(ax, s_pos(1), s_pos(2), s_pos(3), 'rs', 'MarkerSize', 6, 'MarkerFaceColor', 'r');
        end
        
        if isempty(pts), setup_axes(); return; end
        
        % Shift & Filter
        pts_shifted = pts;
        pts_shifted(:,1) = pts_shifted(:,1) - robot_x_now;
        dists = sqrt(sum(pts_shifted.^2, 2));
        keep_mask = dists <= VISIBILITY_RADIUS;
        pts_vis = pts_shifted(keep_mask, :);
        dists_vis = dists(keep_mask);
        
        if isempty(pts_vis), setup_axes(); return; end

        % Colors
        colors = repmat([0, 1, 0], size(pts_vis, 1), 1);
        colors(dists_vis < DIST_WARNING, :) = repmat([1, 1, 0], sum(dists_vis < DIST_WARNING), 1);
        colors(dists_vis < DIST_DANGER, :) = repmat([1, 0, 0], sum(dists_vis < DIST_DANGER), 1);
        
        scatter3(ax, pts_vis(:,1), pts_vis(:,2), pts_vis(:,3), 5, colors, 'filled');
        setup_axes();
    end

    function setup_axes()
        grid(ax, 'on'); axis(ax, 'equal');
        title(ax, 'Obstacle Course (Realistic 4m Range)');
        xlabel(ax, 'X (Forward) [meters]'); ylabel(ax, 'Y (Left) [meters]'); zlabel(ax, 'Z (Height) [meters]');
        
        % Visual range limited to 4.0m
        lim = VISIBILITY_RADIUS;
        xlim(ax, [-lim, lim]); 
        
        % Y-Axis still large to show coordinates correctly, even if empty
        ylim(ax, [-9, 9]); 
        
        zlim(ax, [0, 2.5]); 
        view(ax, 3);
    end

    %% --- REALISTIC DATA GENERATOR ---
    function data = generate_realistic_data(sensors, obstacles, hall_width, vel)
        duration = 25; % Extended duration to get through the gauntlet
        dt = 0.20;     
        steps = duration / dt;
        data = [];
        fov = deg2rad(45); zones_per_axis = 8;
        ang_vals = linspace(-fov/2, fov/2, zones_per_axis);
        [az_grid, el_grid] = meshgrid(ang_vals, ang_vals);
        ray_dirs = [ones(64,1), tan(az_grid(:)), tan(el_grid(:))];
        ray_dirs = ray_dirs ./ vecnorm(ray_dirs, 2, 2);
        
        fprintf('Simulating Gauntlet... ');
        for i = 1:steps
            t = i * dt; rx = vel * t; ry = 0; rz = 0;
            for s = 1:6
                s_yaw = deg2rad(sensors(s).yaw); s_pos = sensors(s).off;
                sx = rx + s_pos(1); sy = ry + s_pos(2); sz = rz + s_pos(3);
                R_s = [cos(s_yaw) -sin(s_yaw) 0; sin(s_yaw) cos(s_yaw) 0; 0 0 1];
                global_rays = (R_s * ray_dirs')'; 
                for z = 1:64
                    ray = global_rays(z, :); 
                    min_dist = 6.0; % Simulation cutoff (slightly > hardware limit)
                    hit = false;
                    
                    % Walls (will likely be too far, but calc anyway)
                    if ray(2)>0, d=(hall_width/2-sy)/ray(2); if d>0&&d<min_dist, min_dist=d; hit=true; end, end
                    if ray(2)<0, d=(-hall_width/2-sy)/ray(2); if d>0&&d<min_dist, min_dist=d; hit=true; end, end
                    
                    % Obstacles
                    for b = 1:size(obstacles, 1)
                        bx=obstacles(b,1); by=obstacles(b,2); bw=obstacles(b,3); bd=obstacles(b,4); bh=obstacles(b,5);
                        b_min=[bx-bw/2, by-bd/2, 0]; b_max=[bx+bw/2, by+bd/2, bh];
                        inv_dir=1./ray; t1=(b_min(1)-sx)*inv_dir(1); t2=(b_max(1)-sx)*inv_dir(1);
                        t3=(b_min(2)-sy)*inv_dir(2); t4=(b_max(2)-sy)*inv_dir(2); t5=(b_min(3)-sz)*inv_dir(3); t6=(b_max(3)-sz)*inv_dir(3);
                        tmin=max([min(t1,t2), min(t3,t4), min(t5,t6)]); tmax=min([max(t1,t2), max(t3,t4), max(t5,t6)]);
                        if tmax>=tmin && tmin>0 && tmin<min_dist, min_dist=tmin; hit=true; end
                    end
                    if hit
                        noise = (rand()-0.5)*0.03;
                        data = [data; t, s, ray_dirs(z,:)*(min_dist+noise)];
                    end
                end
            end
            if mod(i, 20)==0, fprintf('.'); end
        end
        fprintf(' Done!\n');
    end
end