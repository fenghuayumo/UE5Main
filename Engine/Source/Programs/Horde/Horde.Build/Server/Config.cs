// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Serialization;
using Horde.Build.Api;
using Horde.Build.Models;
using Horde.Build.Tools;
using Horde.Build.Utilities;

namespace Horde.Build.Server
{
	using ProjectId = StringId<IProject>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Global configuration
	/// </summary>
	[JsonSchema("https://unrealengine.com/horde/global")]
	[JsonSchemaCatalog("Horde Globals", "Horde global configuration file", "globals.json")]
	public class GlobalConfig
	{
		/// <summary>
		/// List of projects
		/// </summary>
		public List<ProjectConfigRef> Projects { get; set; } = new List<ProjectConfigRef>();

		/// <summary>
		/// List of scheduled downtime
		/// </summary>
		public List<ScheduledDowntime> Downtime { get; set; } = new List<ScheduledDowntime>();

		/// <summary>
		/// List of Perforce clusters
		/// </summary>
		public List<PerforceCluster> PerforceClusters { get; set; } = new List<PerforceCluster>();

		/// <summary>
		/// List of costs of a particular agent type
		/// </summary>
		public List<AgentRateConfig> Rates { get; set; } = new List<AgentRateConfig>();

		/// <summary>
		/// List of compute profiles
		/// </summary>
		public List<ComputeClusterConfig> Compute { get; set; } = new List<ComputeClusterConfig>();

		/// <summary>
		/// List of tools hosted by the server
		/// </summary>
		public List<ToolOptions> Tools { get; set; } = new List<ToolOptions>();

		/// <summary>
		/// Maximum number of conforms to run at once
		/// </summary>
		public int MaxConformCount { get; set; }

		/// <summary>
		/// List of storage namespaces
		/// </summary>
		public StorageConfig? Storage { get; set; }

		/// <summary>
		/// Access control list
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Profile for executing compute requests
	/// </summary>
	public class ComputeClusterConfig
	{
		/// <summary>
		/// Name of the partition
		/// </summary>
		public string Id { get; set; } = "default";

		/// <summary>
		/// Name of the namespace to use
		/// </summary>
		public string NamespaceId { get; set; } = "horde.compute";

		/// <summary>
		/// Name of the input bucket
		/// </summary>
		public string RequestBucketId { get; set; } = "requests";

		/// <summary>
		/// Name of the output bucket
		/// </summary>
		public string ResponseBucketId { get; set; } = "responses";

		/// <summary>
		/// Access control list
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// References a project configuration
	/// </summary>
	public class ProjectConfigRef
	{
		/// <summary>
		/// Unique id for the project
		/// </summary>
		[Required]
		public ProjectId Id { get; set; } = ProjectId.Empty;

		/// <summary>
		/// Config path for the project
		/// </summary>
		[Required]
		public string Path { get; set; } = null!;
	}

	/// <summary>
	/// Stores configuration for a project
	/// </summary>
	[JsonSchema("https://unrealengine.com/horde/project")]
	[JsonSchemaCatalog("Horde Project", "Horde project configuration file", new[] { "*.project.json", "Projects/*.json" })]
	public class ProjectConfig
	{
		/// <summary>
		/// Name for the new project
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Path to the project logo
		/// </summary>
		public string? Logo { get; set; }

		/// <summary>
		/// Categories to include in this project
		/// </summary>
		public List<CreateProjectCategoryRequest> Categories { get; set; } = new List<CreateProjectCategoryRequest>();

		/// <summary>
		/// List of streams
		/// </summary>
		public List<StreamConfigRef> Streams { get; set; } = new List<StreamConfigRef>();

		/// <summary>
		/// Acl entries
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Reference to configuration for a stream
	/// </summary>
	public class StreamConfigRef
	{
		/// <summary>
		/// Unique id for the stream
		/// </summary>
		[Required]
		public StreamId Id { get; set; } = StreamId.Empty;

		/// <summary>
		/// Path to the configuration file
		/// </summary>
		[Required]
		public string Path { get; set; } = null!;
	}

	/// <summary>
	/// Config for a stream
	/// </summary>
	[JsonSchema("https://unrealengine.com/horde/stream")]
	[JsonSchemaCatalog("Horde Stream", "Horde stream configuration file", new[] { "*.stream.json", "Streams/*.json" })]
	public class StreamConfig
	{
		/// <summary>
		/// Name of the stream
		/// </summary>
		[Required]
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// The perforce cluster containing the stream
		/// </summary>
		public string? ClusterName { get; set; }

		/// <summary>
		/// Order for this stream
		/// </summary>
		public int? Order { get; set; }

		/// <summary>
		/// Notification channel for all jobs in this stream
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Channel to post issue triage notifications
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// Default template for running preflights
		/// </summary>
		public string? DefaultPreflightTemplate { get; set; }

		/// <summary>
		/// Default template for running preflights
		/// </summary>
		public DefaultPreflightRequest? DefaultPreflight { get; set; }

		/// <summary>
		/// List of tabs to show for the new stream
		/// </summary>
		public List<CreateStreamTabRequest> Tabs { get; set; } = new List<CreateStreamTabRequest>();

		/// <summary>
		/// Map of agent name to type
		/// </summary>
		public Dictionary<string, CreateAgentTypeRequest> AgentTypes { get; set; } = new Dictionary<string, CreateAgentTypeRequest>();

		/// <summary>
		/// Map of workspace name to type
		/// </summary>
		public Dictionary<string, CreateWorkspaceTypeRequest> WorkspaceTypes { get; set; } = new Dictionary<string, CreateWorkspaceTypeRequest>();

		/// <summary>
		/// List of templates to create
		/// </summary>
		public List<CreateTemplateRefRequest> Templates { get; set; } = new List<CreateTemplateRefRequest>();

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }

		/// <summary>
		/// Pause stream builds until specified date
		/// </summary>
		public DateTime? PausedUntil { get; set; }

		/// <summary>
		/// Reason for pausing builds of the stream
		/// </summary>
		public string? PauseComment { get; set; }

		/// <summary>
		/// How to replicate data from VCS to Horde Storage.
		/// </summary>
		public ContentReplicationMode ReplicationMode { get; set; }

		/// <summary>
		/// Filter for paths to be replicated to storage, as a Perforce wildcard relative to the root of the workspace.
		/// </summary>
		public string? ReplicationFilter { get; set; }
	}

	/// <summary>
	/// Configuration for storage system
	/// </summary>
	public class StorageConfig
	{
		/// <summary>
		/// List of storage namespaces
		/// </summary>
		public List<NamespaceConfig> Namespaces { get; set; } = new List<NamespaceConfig>();
	}

	/// <summary>
	/// Configuration for a storage namespace
	/// </summary>
	public class NamespaceConfig
	{
		/// <summary>
		/// Identifier for this namespace
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// Buckets within this namespace
		/// </summary>
		public List<BucketConfig> Buckets { get; set; } = new List<BucketConfig>();

		/// <summary>
		/// Access control for this namespace
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Configuration for a bucket
	/// </summary>
	public class BucketConfig
	{
		/// <summary>
		/// Identifier for the bucket
		/// </summary>
		public string Id { get; set; } = String.Empty;
	}

	/// <summary>
	/// Describes the monetary cost of agents matching a particular criteris
	/// </summary>
	public class AgentRateConfig
	{
		/// <summary>
		/// Condition string
		/// </summary>
		[CbField("c")]
		public Condition? Condition { get; set; }

		/// <summary>
		/// Rate for this agent
		/// </summary>
		[CbField("r")]
		public double Rate { get; set; }
	}
}

