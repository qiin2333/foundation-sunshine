/**
 * @file src/config_operations.h
 * @brief 配置文件操作（导入/导出/重置）
 * 
 * 该模块提供安全的配置文件操作，可以从 Rust 托盘和 C++ 代码中调用。
 * 从 system_tray.cpp 迁移而来，保留了完整的功能实现。
 */
#pragma once

#include <string>

namespace config_operations {

  /**
   * @brief 从用户选择的文件导入配置
   * 
   * 打开文件对话框让用户选择 .conf 文件，
   * 验证文件，创建备份，然后替换当前配置。
   * 显示适当的成功/错误消息。
   */
  void import_config();

  /**
   * @brief 将当前配置导出到用户选择的文件
   * 
   * 打开保存文件对话框让用户选择目标位置，
   * 然后将当前配置写入该文件。
   * 显示适当的成功/错误消息。
   */
  void export_config();

  /**
   * @brief 将配置重置为默认值
   * 
   * 显示确认对话框，如果确认则将配置文件
   * 重置为默认值。
   * 显示适当的成功/错误消息。
   */
  void reset_config();

}  // namespace config_operations
