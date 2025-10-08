# AI Tuning System for Mouse Settings

## Overview

The AI Tuning System is an advanced feature that automatically fine-tunes all mouse settings using reinforcement learning. It adapts to different games, weapons, and playstyles by learning from your performance and continuously optimizing mouse parameters.

## Features

### Three Aim Modes

1. **Aim Assist** - Subtle assistance with moderate settings
   - Designed for players who want minimal assistance
   - Balanced performance with natural feel
   - Good for competitive play

2. **Aim Bot** - Good lock-on with more aggressive settings
   - Stronger assistance for better target acquisition
   - Optimized for consistent performance
   - Suitable for most gaming scenarios

3. **Rage Baiter** - Maximum performance with most aggressive settings
   - Highest level of assistance
   - Maximum speed and precision
   - For situations requiring peak performance

### Automatic Calibration

- **Auto Calibration**: Starts with very low sensitivity and gradually increases until optimal performance is achieved
- **Game/Weapon Adaptation**: Automatically adjusts when switching games or weapons
- **Real-time Learning**: Continuously improves based on your performance

### Reinforcement Learning

The AI system uses a reward-based learning approach:

- **Positive Rewards**: When the mouse is on target within the detection radius
- **Negative Rewards**: When the mouse is far from targets
- **Adaptive Learning**: Adjusts settings based on success/failure patterns

## Configuration

### Basic Settings

```ini
# Enable AI Tuning
ai_tuning_enabled = true

# Choose aim mode
ai_aim_mode = aim_assist  # aim_assist, aim_bot, or rage_baiter

# Learning parameters
ai_learning_rate = 0.01
ai_exploration_rate = 0.1
ai_training_iterations = 1000
ai_target_radius = 10.0
ai_auto_calibrate = true
```

### Settings Bounds

```ini
# Define the range of values the AI can explore
ai_sensitivity_min = 0.1
ai_sensitivity_max = 5.0
ai_dpi_min = 400
ai_dpi_max = 16000
```

## How It Works

### 1. Initialization
- The AI starts with mode-specific default settings
- Sets up learning parameters and bounds
- Begins training if enabled

### 2. Feedback Loop
- Monitors mouse position relative to detected targets
- Calculates rewards based on accuracy
- Updates settings based on performance

### 3. Learning Process
- **Exploration**: Tries random settings within bounds
- **Exploitation**: Improves successful settings
- **Adaptation**: Adjusts to changing conditions

### 4. Calibration Mode
- Starts with very low sensitivity
- Gradually increases until optimal performance
- Useful when switching games or weapons

## UI Controls

### AI Tuning Tab
- **Enable/Disable**: Toggle AI tuning on/off
- **Aim Mode Selection**: Choose between the three modes
- **Training Parameters**: Adjust learning rate, exploration rate, etc.
- **Training Control**: Start, stop, pause, or reset training
- **Statistics**: View current performance metrics
- **Current Settings**: See AI-tuned values in real-time

### Key Features
- **Real-time Statistics**: Success rate, average reward, best reward
- **Progress Tracking**: Training iteration progress
- **Settings Display**: Current AI-optimized values
- **Apply Best Settings**: Use the best-performing configuration
- **Reset to Defaults**: Return to mode-specific defaults

## Tuned Parameters

The AI system optimizes all mouse-related settings:

### Core Mouse Settings
- **DPI**: Mouse sensitivity hardware setting
- **Sensitivity**: Software sensitivity multiplier
- **Min/Max Speed Multipliers**: Movement speed bounds
- **Prediction Interval**: Target prediction timing

### Target Correction
- **Snap Radius**: Inner precision zone
- **Near Radius**: Smooth slowdown zone
- **Speed Curve Exponent**: Movement curve shape
- **Snap Boost Factor**: Precision boost multiplier

### Wind Mouse (Natural Movement)
- **Gravity Force**: Pull toward target
- **Wind Fluctuation**: Randomness for natural feel
- **Max Step**: Velocity limiting
- **Distance Threshold**: Behavior change point

### Anti-Recoil
- **Easy No Recoil**: Automatic recoil compensation
- **Recoil Strength**: Compensation intensity

## Usage Tips

### Getting Started
1. Enable AI tuning in the overlay
2. Choose your preferred aim mode
3. Start training and let it run for a few minutes
4. Monitor the statistics to see improvement

### Calibration
1. Use "Start Calibration" when switching games
2. Let it run until sensitivity feels optimal
3. Stop calibration when satisfied

### Optimization
1. Play normally while training is active
2. The AI learns from your performance
3. Settings improve automatically over time
4. Use "Apply Best Settings" to save optimal configuration

### Best Practices
- **Let it learn**: Allow time for the AI to adapt
- **Monitor statistics**: Watch success rate and rewards
- **Use calibration**: When switching games or weapons
- **Save good settings**: Apply best settings when satisfied
- **Reset when needed**: If performance degrades

## Technical Details

### Reward Calculation
```
Reward = 1.0 - (distance_to_target / target_radius) * 0.5
```
- Maximum reward when on target
- Decreases with distance from target
- Penalty for missing targets

### Learning Algorithm
- **Q-Learning**: Reinforcement learning approach
- **Exploration vs Exploitation**: Balanced random vs optimal choices
- **Adaptive Bounds**: Settings stay within defined limits
- **Continuous Learning**: Real-time adaptation

### Performance Metrics
- **Success Rate**: Percentage of time on target
- **Average Reward**: Mean performance score
- **Best Reward**: Peak performance achieved
- **Training Progress**: Iteration count and completion

## Troubleshooting

### Common Issues

**AI not learning**
- Check if training is enabled
- Verify target detection is working
- Ensure sufficient training time

**Settings too extreme**
- Adjust bounds in configuration
- Use calibration mode
- Reset to defaults if needed

**Performance degradation**
- Reset training to start fresh
- Check for conflicting settings
- Verify aim mode selection

### Performance Tips
- **Stable FPS**: Ensure consistent frame rates
- **Good Detection**: Verify target detection accuracy
- **Sufficient Training**: Allow time for learning
- **Regular Calibration**: When changing games/weapons

## Advanced Configuration

### Custom Bounds
Adjust the AI's exploration range for specific needs:

```ini
# Conservative bounds for subtle tuning
ai_sensitivity_min = 0.5
ai_sensitivity_max = 2.0
ai_dpi_min = 800
ai_dpi_max = 1600

# Aggressive bounds for major optimization
ai_sensitivity_min = 0.1
ai_sensitivity_max = 10.0
ai_dpi_min = 400
ai_dpi_max = 16000
```

### Learning Parameters
Fine-tune the learning behavior:

```ini
# Fast learning (more aggressive changes)
ai_learning_rate = 0.05
ai_exploration_rate = 0.2

# Slow learning (gradual changes)
ai_learning_rate = 0.005
ai_exploration_rate = 0.05
```

## Conclusion

The AI Tuning System provides an intelligent way to optimize mouse settings for any game or situation. By learning from your performance and adapting in real-time, it ensures optimal aiming performance while maintaining the feel you prefer. Whether you need subtle assistance or maximum performance, the AI system can adapt to your needs.
