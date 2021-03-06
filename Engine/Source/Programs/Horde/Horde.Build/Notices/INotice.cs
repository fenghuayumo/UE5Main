// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using Horde.Build.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

namespace Horde.Build.Models
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// User notice
	/// </summary>
	public interface INotice
	{
		/// <summary>
		/// Unique id for this reservation
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// User id for who created this notice, null for system created notices
		/// </summary>
		[BsonIgnoreIfNull]
		public UserId? UserId { get; }

		/// <summary>
		/// Start time to display this message
		/// </summary>
		public DateTime? StartTime { get; }

		/// <summary>
		/// Finish time to display this message
		/// </summary>
		public DateTime? FinishTime { get;  }

		/// <summary>
		/// Message to display
		/// </summary>
		public string Message { get; }
	}
}
