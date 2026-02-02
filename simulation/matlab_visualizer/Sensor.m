classdef Sensor
    % Sensor
    % Represents the pose (position + orientation) of a ToF sensor.
    %
    %   Sensor(x, y, z, alpha, beta, gamma)
    %
    %   Position (mm):
    %       x     - X offset in world frame
    %       y     - Y offset in world frame (forward is +Y)
    %       z     - Z offset in world frame (up is +Z)
    %
    %   Orientation (degrees):
    %       alpha - rotation about X-axis   (roll)
    %       beta  - rotation about Y-axis   (yaw)
    %       gamma - rotation about Z-axis   (pitch)
    %
    %   Methods:
    %       getTransform() - returns 4x4 homogeneous transform for this sensor
    
    
    properties
        % Position offsets in world frame (mm)
        x
        y
        z

        % Orientation angles (degrees)
        alpha   % rotation around X-axis
        beta    % rotation around Y-axis
        gamma   % rotation around Z-axis
    end
    
    
    methods
        % Constructor ------------------------------------------------------
        function obj = Sensor(x, y, z, alpha, beta, gamma)
            % Create a sensor with a given position and orientation.
            %
            % Example:
            %   s = Sensor(0,0,500, 0,0,-15);
            
            obj.x = x;
            obj.y = y;
            obj.z = z;
            
            obj.alpha = alpha;
            obj.beta  = beta;
            obj.gamma = gamma;
        end
        
        
        % Compute 4x4 transform matrix -------------------------------------
        function T = getTransform(obj)
            % Returns the 4x4 homogeneous transform of the sensor
            % relative to the world frame.
            %
            % Output:
            %   T = [R, d;
            %        0 0 0 1]
            %
            % where:
            %   R = rotation matrix
            %   d = [x; y; z] position
            
            % Degree-based rotation matrices
            Rx = @(a)[1 0 0;
                      0 cosd(a) -sind(a);
                      0 sind(a)  cosd(a)];

            Rz = @(g)[cosd(g) -sind(g) 0;
                      sind(g)  cosd(g) 0;
                      0        0       1];

            Ry = @(b)[ cosd(b) 0 sind(b);
                       0       1 0;
                      -sind(b) 0 cosd(b)];
            
            % Complete rotation
            R =  Rz(obj.gamma)* Rx(obj.alpha) * Ry(obj.beta);

            % Position vector
            d = [obj.x; obj.y; obj.z];

            % 4x4 homogeneous transform
            T = [R, d; 0 0 0 1];
        end
    end
end
